// The page served in "Add books" mode. It works offline (the phone is on the
// stick's network, with no internet), so it has no external scripts: EPUBs are
// unzipped with the browser's own DecompressionStream and turned into plain
// text before upload, which keeps the firmware small.
#pragma once

static const char kUploadPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Speed Reader</title>
<style>
  :root { --bg:#111; --card:#1c1c1c; --text:#eee; --dim:#999; --line:#333; --accent:#ff4838; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:16px/1.45 -apple-system, system-ui, sans-serif; }
  main { max-width:560px; margin:0 auto; padding:24px 16px 48px; }
  h1 { font-size:22px; margin:0 0 4px; }
  h1 b { color:var(--accent); }
  p.sub { color:var(--dim); margin:0 0 20px; }
  .drop { display:block; border:2px dashed var(--line); border-radius:12px; padding:28px 16px; text-align:center; cursor:pointer; }
  .drop.over { border-color:var(--accent); }
  .drop strong { display:block; font-size:18px; margin-bottom:4px; }
  .drop span { color:var(--dim); font-size:14px; }
  input[type=file] { display:none; }
  #jobs { margin:16px 0 0; padding:0; list-style:none; }
  #jobs li { background:var(--card); border-radius:8px; padding:10px 12px; margin-bottom:8px; font-size:14px; }
  #jobs .bar { height:4px; background:var(--line); border-radius:2px; margin-top:6px; overflow:hidden; }
  #jobs .bar i { display:block; height:100%; width:0; background:var(--accent); transition:width .2s; }
  #jobs .err { color:var(--accent); }
  h2 { font-size:15px; color:var(--dim); font-weight:600; margin:32px 0 8px; display:flex; justify-content:space-between; }
  #books { list-style:none; margin:0; padding:0; }
  #books li { display:flex; align-items:center; gap:12px; padding:10px 0; border-bottom:1px solid var(--line); }
  #books .name { flex:1; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  #books .size { color:var(--dim); font-size:13px; }
  #books button { background:none; border:1px solid var(--line); color:var(--dim); border-radius:6px; padding:4px 10px; font-size:13px; }
  #books button:hover { color:var(--accent); border-color:var(--accent); }
  .empty { color:var(--dim); padding:10px 0; }
</style>
</head>
<body>
<main>
  <h1>Speed <b>Reader</b></h1>
  <p class="sub">Add books to your stick. Your phone may say there's no internet; that's expected.</p>

  <label class="drop" id="drop">
    <strong>Choose books</strong>
    <span>EPUB or TXT. You can pick several.</span>
    <input type="file" id="file" accept=".epub,.txt,application/epub+zip,text/plain" multiple>
  </label>
  <ul id="jobs"></ul>

  <h2><span>On the stick</span><span id="space"></span></h2>
  <ul id="books"><li class="empty">Loading...</li></ul>
</main>
<script>
const $ = (s) => document.querySelector(s);

function fmtSize(n) {
  if (n < 1024) return n + ' B';
  if (n < 1048576) return (n / 1024).toFixed(0) + ' KB';
  return (n / 1048576).toFixed(1) + ' MB';
}

// ---- EPUB -> text --------------------------------------------------------

async function openZip(buf) {
  const dv = new DataView(buf);
  let eocd = -1;
  for (let i = buf.byteLength - 22; i >= Math.max(0, buf.byteLength - 65557); i--) {
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error('not a valid EPUB file');
  const count = dv.getUint16(eocd + 10, true);
  let p = dv.getUint32(eocd + 16, true);
  const utf8 = new TextDecoder();
  const files = {};
  for (let n = 0; n < count && dv.getUint32(p, true) === 0x02014b50; n++) {
    const nameLen = dv.getUint16(p + 28, true);
    const name = utf8.decode(new Uint8Array(buf, p + 46, nameLen));
    files[name] = {
      method: dv.getUint16(p + 10, true),
      size: dv.getUint32(p + 20, true),
      offset: dv.getUint32(p + 42, true),
    };
    p += 46 + nameLen + dv.getUint16(p + 30, true) + dv.getUint16(p + 32, true);
  }
  return {
    has: (name) => name in files,
    async text(name) {
      const f = files[name];
      if (!f) return null;
      const start = f.offset + 30 + dv.getUint16(f.offset + 26, true) + dv.getUint16(f.offset + 28, true);
      const raw = new Uint8Array(buf, start, f.size);
      if (f.method === 0) return utf8.decode(raw);
      if (f.method !== 8) throw new Error('unsupported compression in EPUB');
      if (typeof DecompressionStream === 'undefined') throw new Error('this browser is too old to read EPUBs; try a newer one or upload a .txt');
      const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
      return await new Response(stream).text();
    },
  };
}

function resolvePath(base, href) {
  const parts = (base + href.split('#')[0]).split('/');
  const out = [];
  for (const part of parts) {
    if (part === '..') out.pop();
    else if (part !== '.' && part !== '') out.push(part);
  }
  return decodeURIComponent(out.join('/'));
}

function parseXml(text, type) {
  const doc = new DOMParser().parseFromString(text, type);
  return doc.getElementsByTagName('parsererror').length ? null : doc;
}

const BLOCKS = new Set(['p','div','section','article','h1','h2','h3','h4','h5','h6','li','blockquote',
  'br','hr','tr','pre','dd','dt','figcaption','header','footer','aside','table']);
const SKIP = new Set(['script','style','head','title','svg','math','rt','rp']);

function htmlToText(markup) {
  const doc = parseXml(markup, 'application/xhtml+xml') || new DOMParser().parseFromString(markup, 'text/html');
  const out = [];
  const walk = (node) => {
    for (const child of node.childNodes) {
      if (child.nodeType === 3) out.push(child.nodeValue);
      else if (child.nodeType === 1) {
        const tag = child.localName.toLowerCase();
        if (SKIP.has(tag)) continue;
        const block = BLOCKS.has(tag);
        if (block) out.push('\n\n');
        walk(child);
        if (block) out.push('\n\n');
      }
    }
  };
  walk(doc.body || doc.documentElement);
  return out.join('');
}

async function epubToText(buf) {
  const zip = await openZip(buf);
  const container = parseXml(await zip.text('META-INF/container.xml') || '', 'application/xml');
  const rootfile = container && container.getElementsByTagNameNS('*', 'rootfile')[0];
  if (!rootfile) throw new Error('EPUB has no table of contents');
  const opfPath = rootfile.getAttribute('full-path');
  const opf = parseXml(await zip.text(opfPath) || '', 'application/xml');
  if (!opf) throw new Error('EPUB package is damaged');
  const base = opfPath.includes('/') ? opfPath.slice(0, opfPath.lastIndexOf('/') + 1) : '';

  const manifest = {};
  for (const item of opf.getElementsByTagNameNS('*', 'item')) manifest[item.getAttribute('id')] = item.getAttribute('href');
  const titleEl = opf.getElementsByTagNameNS('*', 'title')[0];
  const title = titleEl ? titleEl.textContent.trim() : '';

  const chapters = [];
  for (const ref of opf.getElementsByTagNameNS('*', 'itemref')) {
    if (ref.getAttribute('linear') === 'no') continue;
    const href = manifest[ref.getAttribute('idref')];
    if (!href) continue;
    const markup = await zip.text(resolvePath(base, href));
    if (markup) chapters.push(htmlToText(markup));
  }
  if (!chapters.length) throw new Error('found no chapters in this EPUB');
  return { title, text: chapters.join('\n\n') };
}

// ---- Text clean-up --------------------------------------------------------

function cleanText(text) {
  return text
    .replace(/\r\n?/g, '\n')
    .normalize('NFKD').replace(/[\u0300-\u036f]/g, '')     // e with accent -> e
    .replace(/[\u00a0\u2000-\u200a\u202f\u205f\u3000\t\f\v]/g, ' ')
    .replace(/[\u00ad\u200b-\u200d\ufeff]/g, '')           // soft hyphens, zero-width
    .replace(/[ \t]*[\u2014\u2015][ \t]*/g, '\u2014 ')      // "said\u2014quietly" -> two words
    .replace(/ +/g, ' ')
    .replace(/ *\n */g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    .trim() + '\n';
}

function decodeText(buf) {
  try { return new TextDecoder('utf-8', { fatal: true }).decode(buf); }
  catch { return new TextDecoder('windows-1252').decode(buf); }
}

// ---- Upload ----------------------------------------------------------------

function addJob(label) {
  const li = document.createElement('li');
  li.innerHTML = '<div class="label"></div><div class="bar"><i></i></div>';
  li.querySelector('.label').textContent = label;
  $('#jobs').prepend(li);
  return {
    say(text, isError) {
      const l = li.querySelector('.label');
      l.textContent = text;
      l.className = 'label' + (isError ? ' err' : '');
    },
    progress(f) { li.querySelector('.bar i').style.width = Math.round(f * 100) + '%'; },
  };
}

function send(name, text, job) {
  const blob = new Blob([text], { type: 'text/plain' });
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/upload?size=' + blob.size);
    xhr.upload.onprogress = (e) => e.lengthComputable && job.progress(e.loaded / e.total);
    xhr.onload = () => xhr.status === 200 ? resolve(xhr.responseText) : reject(new Error(xhr.responseText || 'upload failed'));
    xhr.onerror = () => reject(new Error('lost connection to the stick'));
    const form = new FormData();
    form.append('file', blob, name + '.txt');
    xhr.send(form);
  });
}

async function addBook(file) {
  const baseName = file.name.replace(/\.[^.]+$/, '');
  const job = addJob('Reading ' + file.name + '...');
  try {
    const buf = await file.arrayBuffer();
    let title = baseName, text;
    if (/\.epub$/i.test(file.name)) {
      const book = await epubToText(buf);
      text = book.text;
      if (book.title) title = book.title.normalize('NFKD').replace(/[\u0300-\u036f]/g, '');
    } else {
      text = decodeText(buf);
    }
    text = cleanText(text);
    const nonAscii = (text.match(/[^\x00-\x7f]/g) || []).length;
    if (nonAscii > text.length * 0.3) throw new Error("this book's alphabet isn't supported yet (Latin letters only)");
    job.say('Sending ' + title + ' (' + fmtSize(text.length) + ')...');
    await send(title, text, job);
    job.progress(1);
    job.say('Added ' + title);
    loadBooks();
  } catch (e) {
    job.say(file.name + ': ' + e.message, true);
  }
}

async function addBooks(files) {
  for (const f of files) await addBook(f);  // one at a time; the stick has one radio
}

async function loadBooks() {
  try {
    const res = await fetch('/api/books');
    const data = await res.json();
    const list = $('#books');
    list.innerHTML = '';
    if (!data.books.length) list.innerHTML = '<li class="empty">No books yet.</li>';
    for (const b of data.books) {
      const li = document.createElement('li');
      li.innerHTML = '<span class="name"></span><span class="size"></span><button>Remove</button>';
      li.querySelector('.name').textContent = b.name.replace(/\.txt$/, '');
      li.querySelector('.size').textContent = fmtSize(b.size);
      li.querySelector('button').onclick = async () => {
        if (!confirm('Remove "' + b.name.replace(/\.txt$/, '') + '" from the stick?')) return;
        await fetch('/api/delete?name=' + encodeURIComponent(b.name), { method: 'POST' });
        loadBooks();
      };
      list.appendChild(li);
    }
    $('#space').textContent = fmtSize(data.free) + ' free';
  } catch {
    $('#books').innerHTML = '<li class="empty">Can\'t reach the stick. Is it still in "Add books" mode?</li>';
  }
}

$('#file').onchange = (e) => { addBooks([...e.target.files]); e.target.value = ''; };
const drop = $('#drop');
drop.ondragover = (e) => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove('over'); addBooks([...e.dataTransfer.files]); };
loadBooks();
</script>
</body>
</html>
)HTML";
