# ESP Web Tools – saját fordítás

Az [ESP Web Tools](https://github.com/esphome/esp-web-tools) 10.4.0 (Apache-2.0, `LICENSE`) változatlan forrása,
de az esptool-js **0.7.0**-val fordítva (a kiadott 10.4.0-ban 0.6.x van).

Miért: az esptool-js 0.6.x a csip felismeréséhez a `0x40001000` címet olvassa, és ettől az ESP32-P4 v3.x
(a JC8012P4A1C-n v3.2) elnémul: „Serial data stream stopped”, „Failed to initialize”. A 0.7.0 előbb a
`GET_SECURITY_INFO` csipazonosítójával ismer fel (mint a Python-os esptool).

Fordítás (Node.js és jq kell):

```
git clone --depth 1 --branch 10.4.0 https://github.com/esphome/esp-web-tools.git
cd esp-web-tools
# package.json: "esptool-js": "0.7.0"
npm install
bash script/build
# a dist/web/*.js fájlok ide kerülnek
```

Ha az ESP Web Tools új kiadásában már esptool-js 0.7 (vagy újabb) van, ez a mappa törölhető, és az `index.html`
mehet vissza az unpkg-re.
