// upload_ue.js — upload a local skin/pak file to the keyboard
//
// Usage:
//   node upload_ue.js <path> [--name <name>] [--slot <0|1|2>] [--id <uuid>]
//
// Exact sequence mirrored from browser capture:
//   1. JSON_START (0x01) → FILE_DATA (json bytes) → FILE_END (0x20)
//   2. SKIN_START (0x00) → FILE_DATA (file chunks) → FILE_END (0x20) → FILE_STATUS (0x11)

'use strict';
const HID    = require('node-hid');
const fs     = require('fs');
const path   = require('path');
const crypto = require('crypto');

function uuidv4() {
  const b = crypto.randomBytes(16);
  b[6] = (b[6] & 0x0f) | 0x40;
  b[8] = (b[8] & 0x3f) | 0x80;
  return [...b].map((v,i) =>
    ([4,6,8,10].includes(i) ? '-' : '') + v.toString(16).padStart(2,'0')
  ).join('');
}

const VENDOR_ID  = 13853;
const SOM_PID    = 514;
const USAGE_PAGE = 0xFF00;
const USAGE      = 1;
const CHUNK      = 1020;
const MB_BEAT    = 1024 * 1024;

// ── arg parsing ───────────────────────────────────────────────────────────────
function parseArgs() {
  const args = process.argv.slice(2);
  const out  = { file: null, id: null, name: null, slot: 0 };
  for (let i = 0; i < args.length; i++) {
    if      (args[i] === '--id'   && args[i+1]) out.id   = args[++i];
    else if (args[i] === '--name' && args[i+1]) out.name = args[++i];
    else if (args[i] === '--slot' && args[i+1]) out.slot = parseInt(args[++i], 10);
    else if (!args[i].startsWith('--'))         out.file = args[i];
  }
  return out;
}

// ── HID helpers ───────────────────────────────────────────────────────────────
function findMI1() {
  return HID.devices().find(d =>
    d.vendorId === VENDOR_ID && d.productId === SOM_PID &&
    d.usagePage === USAGE_PAGE && d.usage === USAGE
  );
}

function send(dev, cmd, payload = Buffer.alloc(0)) {
  const r = new Array(1024).fill(0);
  r[0] = 0x01;
  r[1] = payload.length & 0xFF;
  r[2] = payload.length >> 8;
  r[3] = cmd;
  for (let i = 0; i < payload.length; i++) r[4 + i] = payload[i];
  dev.write(r);
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function waitResponse(dev, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => {
      dev.removeListener('data', handler);
      reject(new Error(`No response within ${timeoutMs}ms`));
    }, timeoutMs);
    function handler(raw) {
      const d   = Array.from(raw).slice(1);
      const len = d[0] | (d[1] << 8);
      if (len === 0) return;
      clearTimeout(t);
      dev.removeListener('data', handler);
      resolve({ cmd: d[2], pay: Buffer.from(d.slice(3, 3 + len)) });
    }
    dev.on('data', handler);
    dev.on('error', err => { clearTimeout(t); reject(err); });
  });
}

// ── send a complete file transfer (START → DATA chunks → END) ────────────────
async function sendTransfer(dev, startCmd, data, label) {
  const t0   = Date.now();
  let mbBucket = 0;

  send(dev, startCmd);
  await sleep(100);

  process.stdout.write(`  ${label}`);
  for (let offset = 0; offset < data.length; offset += CHUNK) {
    const chunk = data.slice(offset, offset + CHUNK);
    send(dev, 0x10, chunk);
    mbBucket += chunk.length;
    if (mbBucket >= MB_BEAT) { send(dev, 0x11); mbBucket = 0; }
    if ((offset / CHUNK) % 512 === 0 && offset > 0) process.stdout.write('.');
  }

  send(dev, 0x20);  // FILE_END
  const elapsed = (Date.now() - t0) / 1000;
  const mbps    = (data.length / 1024 / 1024 / elapsed).toFixed(2);
  console.log(` done  ${(data.length/1024/1024).toFixed(2)} MB in ${elapsed.toFixed(1)}s (${mbps} MB/s)`);
}

// ── parse device response ─────────────────────────────────────────────────────
function parseResponse(cmd, pay, sentBytes) {
  const hex = [...pay].map(b => b.toString(16).padStart(2,'0')).join(' ');
  const txt = [...pay].map(b => b >= 32 && b < 127 ? String.fromCharCode(b) : '.').join('');
  console.log(`\n← cmd=0x${cmd.toString(16).padStart(2,'0')}  [${hex}]`);
  if (txt.trim()) console.log(`   "${txt}"`);

  // Error: [01 00 01 <msgLen> 00 <message>]
  if (pay.length >= 5 && pay[0] === 0x01 && pay[2] === 0x01) {
    const msg = pay.slice(5, 5 + pay[3]).toString('utf8');
    console.log(`\n✗ ${msg}`);
    return;
  }

  // Success: [byteCount u32LE][CRC32 u32LE]
  if (pay.length >= 8) {
    const rxBytes = pay.readUInt32LE(0);
    const rxCrc   = pay.readUInt32LE(4);
    console.log(`\nDevice bytes : ${rxBytes.toLocaleString()}  (sent: ${sentBytes.toLocaleString()})`);
    console.log(`Device CRC32 : 0x${rxCrc.toString(16).padStart(8,'0')}`);
    console.log(rxBytes === sentBytes ? '\n✓ Transfer complete.' : '\n✗ Byte count mismatch.');
  }
}

// ── main ─────────────────────────────────────────────────────────────────────
async function main() {
  const { file: filePath, id: _id, name, slot } = parseArgs();

  if (!filePath) {
    console.error('Usage: node upload_ue.js <path> [--name <name>] [--slot <0|1|2>] [--id <uuid>]');
    console.error('  --name  display name (defaults to filename without extension)');
    console.error('  --slot  skin slot 0/1/2 (default 0)');
    console.error('  --id    override fileID UUID (auto-generated if omitted)');
    process.exit(1);
  }

  const fileID = _id || uuidv4();
  if (!fs.existsSync(filePath)) { console.error('File not found:', filePath); process.exit(1); }

  const data     = fs.readFileSync(filePath);
  const fileSize = data.length;
  const ext      = path.extname(filePath).replace('.', '').toLowerCase() || 'pak';
  const fileName = name || path.basename(filePath, path.extname(filePath));

  // JSON manifest — exact fields from browser capture
  const manifest = JSON.stringify({
    slot:          slot,
    fileName:      fileName,
    fileExtension: ext,
    fileSize:      fileSize,
    fileID:        fileID,
  });

  console.log(`File     : ${path.basename(filePath)}  (${(fileSize/1024/1024).toFixed(2)} MB)`);
  console.log(`Manifest : ${manifest}`);

  const info = findMI1();
  if (!info) { console.error('\nMI_01 not found — keyboard not connected?'); process.exit(1); }
  console.log(`HID      : ${info.path}\n`);

  const dev = new HID.HID(info.path);
  dev.on('error', err => console.error('[hid error]', err.message));

  // ── Phase 1: JSON_START → JSON bytes → FILE_END ───────────────────────────
  console.log('Phase 1: JSON manifest');
  await sendTransfer(dev, 0x01, Buffer.from(manifest, 'utf8'), 'JSON_START → DATA → END');
  await sleep(200);

  // ── Phase 2: SKIN_START → file bytes → FILE_END → FILE_STATUS ────────────
  console.log('Phase 2: skin file');
  await sendTransfer(dev, 0x00, data, 'SKIN_START → DATA');

  // FILE_STATUS — wait for final confirmation
  await sleep(50);
  send(dev, 0x11);
  console.log('FILE_STATUS sent, awaiting response...');

  try {
    const { cmd, pay } = await waitResponse(dev, 10000);
    parseResponse(cmd, pay, fileSize);
  } catch (e) {
    console.log('?', e.message);
  }

  dev.close();
}

main().catch(err => { console.error(err); process.exit(1); });
