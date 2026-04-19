# Munkamenet kontextus

Létrehozva: 2026-04-19  
Cél: Gree klímák helyi hálózati vezérlése

## Python venv

```
>/home/user/ai-projects/python-venv
```
Telepített csomag: `pycryptodome` (AES titkosításhoz)

## Hálózat

- Alhálózat: `192.168.1.0/24`
- Ez a gép: `192.168.1.113` (myhost.local), WiFi interfész: `wlan0`

## Gree klímák

### Protokoll

- Felfedezés: UDP broadcast port **7000**, JSON csomag: `{"t":"scan"}`
- Válasz: AES-128-ECB titkosított, Base64 kódolt `pack` mező
- Discovery kulcs: `a3K8Bx%2r8Y7#xDh`
- Eszközök a `{"t":"dev", ...}` választ adják vissza, tartalmazza a MAC-et (`cid`)

### Talált klímák

| IP | MAC (cid) | Firmware | Állapot |
|----|-----------|----------|---------|
| 192.168.1.215 | 020000aa0005 | V2.0.0 | aktív |
| 192.168.1.221 | 020000aa0006 | V2.0.0 | aktív |
| 192.168.1.241 | 020000aa0007 | V2.0.0 | aktív |
| TBD | TBD | TBD | 4. klíma — még nem azonosítva |

### Következő lépések

1. 4. klíma azonosítása (be kell kapcsolni, majd újra futtatni a discovery-t)
2. Binding: minden klímával elvégezni a kulcscsere handshake-et (egyedi eszközkulcs)
3. Állapot lekérdezés: `{"t":"status"}` csomag küldése
4. Vezérlő szkript írása (be/ki, hőmérséklet, üzemmód: auto/cool/heat/fan/dry)

### Discovery Python kód

```python
import socket, base64, json
from Crypto.Cipher import AES

DISCOVERY_KEY = b"a3K8Bx%2r8Y7#xDh"
PORT = 7000

def decrypt(pack, key=DISCOVERY_KEY):
    cipher = AES.new(key, AES.MODE_ECB)
    raw = cipher.decrypt(base64.b64decode(pack))
    return json.loads(raw.rstrip(b'\x00').decode('utf-8'))

def scan(broadcast="192.168.1.255"):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(5)
    sock.sendto(b'{"t":"scan"}', (broadcast, PORT))
    devices = []
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            msg = json.loads(data)
            info = decrypt(msg["pack"])
            devices.append({"ip": addr[0], "mac": info["cid"], "ver": info["ver"], "hid": info["hid"]})
    except socket.timeout:
        pass
    sock.close()
    return devices

if __name__ == "__main__":
    for d in scan():
        print(d)
```

## Egyéb figyelemre méltó eszközök

| IP | Eszköz |
|----|--------|
| 192.168.1.218 | robot vacuum (vacuum.local) |
| 192.168.1.230 | smart TV (HTTP server) |
| 192.168.1.120 | nyomtató |
| 192.168.1.130/131 | web server (Linux server, azonos MAC → VM/konténer) |
| 192.168.1.211 | mesh node |
