// ============================================================================
// make-ca.js — Tạo CA nội bộ (root CA) + chứng chỉ server ký bởi CA đó.
// Chạy:  node make-ca.js
// Sinh ra thư mục web/certs/:
//   rootCA.pem       -> CÀI vào thiết bị (PC + điện thoại) để hết cảnh báo HTTPS
//   rootCA-key.pem   -> khoá riêng CA (GIỮ KÍN, chỉ để ký thêm cert khi đổi IP)
//   server.pem       -> chứng chỉ server (đã ký, kèm SAN = IP LAN + localhost + tên máy)
//   server-key.pem   -> khoá riêng server (server.js dùng)
// Cũng copy rootCA.pem sang public/ để điện thoại tải về cài.
//
// Khi ĐỔI IP (DHCP): chạy lại  node make-ca.js  (giữ nguyên rootCA nếu còn) để ký cert mới.
// ============================================================================

const { webcrypto } = require('crypto');
const os = require('os');
const fs = require('fs');
const path = require('path');
const x509 = require('@peculiar/x509');

x509.cryptoProvider.set(webcrypto);

const alg = { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256', publicExponent: new Uint8Array([1, 0, 1]), modulusLength: 2048 };
const YEARS10 = 3650 * 24 * 3600 * 1000;

const CERT_DIR = path.join(__dirname, 'certs');
const PUBLIC_DIR = path.join(__dirname, 'public');

function lanIPs() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces()))
    for (const i of list) if (i.family === 'IPv4' && !i.internal) out.push(i.address);
  return out;
}
function pemPrivateKey(pkcs8) {
  const b64 = Buffer.from(pkcs8).toString('base64').match(/.{1,64}/g).join('\n');
  return `-----BEGIN PRIVATE KEY-----\n${b64}\n-----END PRIVATE KEY-----\n`;
}

(async () => {
  if (!fs.existsSync(CERT_DIR)) fs.mkdirSync(CERT_DIR);

  // --- 1) Root CA: dùng lại nếu đã có (để KHÔNG phải cài lại CA trên thiết bị) ---
  let caKeys, caCert;
  const caCertPath = path.join(CERT_DIR, 'rootCA.pem');
  const caKeyPath = path.join(CERT_DIR, 'rootCA-key.pem');
  if (fs.existsSync(caCertPath) && fs.existsSync(caKeyPath)) {
    caCert = new x509.X509Certificate(fs.readFileSync(caCertPath, 'utf8'));
    const keyDer = pemToDer(fs.readFileSync(caKeyPath, 'utf8'));
    const priv = await webcrypto.subtle.importKey('pkcs8', keyDer, alg, true, ['sign']);
    const pub = await caCert.publicKey.export(webcrypto);
    caKeys = { privateKey: priv, publicKey: pub };
    console.log('>> Dùng lại rootCA sẵn có (không cài lại CA trên thiết bị).');
  } else {
    caKeys = await webcrypto.subtle.generateKey(alg, true, ['sign', 'verify']);
    caCert = await x509.X509CertificateGenerator.createSelfSigned({
      serialNumber: '01',
      name: 'CN=AGV Local CA, O=AGV',
      notBefore: new Date(),
      notAfter: new Date(Date.now() + YEARS10),
      signingAlgorithm: alg,
      keys: caKeys,
      extensions: [
        new x509.BasicConstraintsExtension(true, undefined, true),
        new x509.KeyUsagesExtension(x509.KeyUsageFlags.keyCertSign | x509.KeyUsageFlags.cRLSign, true),
        await x509.SubjectKeyIdentifierExtension.create(caKeys.publicKey),
      ],
    });
    fs.writeFileSync(caCertPath, caCert.toString('pem'));
    const caPkcs8 = await webcrypto.subtle.exportKey('pkcs8', caKeys.privateKey);
    fs.writeFileSync(caKeyPath, pemPrivateKey(caPkcs8));
    console.log('>> Đã tạo rootCA mới.');
  }

  // --- 2) Chứng chỉ server (ký bởi CA), SAN gồm mọi IP LAN + localhost + tên máy ---
  const srvKeys = await webcrypto.subtle.generateKey(alg, true, ['sign', 'verify']);
  const ips = lanIPs();
  const host = os.hostname();
  const sanNames = [
    { type: 'dns', value: 'localhost' },
    { type: 'dns', value: host },
    { type: 'dns', value: host + '.local' },
    { type: 'ip', value: '127.0.0.1' },
    ...ips.map(ip => ({ type: 'ip', value: ip })),
  ];
  const srvCert = await x509.X509CertificateGenerator.create({
    serialNumber: Date.now().toString(),
    subject: 'CN=AGV Server',
    issuer: caCert.subject,
    notBefore: new Date(),
    notAfter: new Date(Date.now() + YEARS10),
    signingAlgorithm: alg,
    publicKey: srvKeys.publicKey,
    signingKey: caKeys.privateKey,
    extensions: [
      new x509.BasicConstraintsExtension(false),
      new x509.KeyUsagesExtension(x509.KeyUsageFlags.digitalSignature | x509.KeyUsageFlags.keyEncipherment),
      new x509.ExtendedKeyUsageExtension(['1.3.6.1.5.5.7.3.1']), // serverAuth
      new x509.SubjectAlternativeNameExtension(sanNames),
      await x509.SubjectKeyIdentifierExtension.create(srvKeys.publicKey),
    ],
  });
  fs.writeFileSync(path.join(CERT_DIR, 'server.pem'), srvCert.toString('pem'));
  const srvPkcs8 = await webcrypto.subtle.exportKey('pkcs8', srvKeys.privateKey);
  fs.writeFileSync(path.join(CERT_DIR, 'server-key.pem'), pemPrivateKey(srvPkcs8));

  // copy rootCA.pem sang public/ để điện thoại tải về cài (https://<ip>:3000/rootCA.pem)
  fs.copyFileSync(caCertPath, path.join(PUBLIC_DIR, 'rootCA.pem'));

  console.log('>> Đã sinh chứng chỉ server cho SAN:', sanNames.map(s => s.value).join(', '));
  console.log('>> Files trong web/certs/ . rootCA.pem cũng đã copy sang public/ (tải qua /rootCA.pem).');
  console.log('>> Bước tiếp: cài rootCA vào thiết bị, rồi khởi động lại server.');
})().catch(e => { console.error('Lỗi tạo CA:', e); process.exit(1); });

function pemToDer(pem) {
  const b64 = pem.replace(/-----[^-]+-----/g, '').replace(/\s+/g, '');
  return Buffer.from(b64, 'base64');
}
