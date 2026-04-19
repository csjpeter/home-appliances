# Hálózati eszközök — 192.168.1.0/24

Felderítés dátuma: 2026-04-19

## Hálózati infrastruktúra

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.1 | router.local | router | Főrouter |
| 192.168.1.211 | — | mesh node | Másodlagos mesh router (MAC: 02:00:00:aa:00:01), SSH + HTTP |

## Számítógépek

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.102 | laptop1.local | laptop | |
| 192.168.1.104 | desktop1.local | PC | |
| 192.168.1.112 | laptop1-wifi | laptop | Valószínűleg ugyanaz mint .102, más interfész |
| 192.168.1.113 | myhost.local | PC | Ez a gép |
| 192.168.1.205 | — | laptop | MAC: 02:00:00:aa:00:02 (vendor), nincs nyitott port |
| 192.168.1.252 | laptop2 | laptop | |

## Szerverek

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.130 | — | Linux server | web server, MAC: 02:00:00:aa:00:03 |
| 192.168.1.131 | — | Linux server | web server, azonos MAC mint .130 — VM/konténer |

## Okostelefonok / tabletek

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.205 | — | laptop | Lásd Számítógépek |
| 192.168.1.212 | phone1 | Android phone | |
| 192.168.1.226 | phone2 | Android phone | |
| 192.168.1.232 | phone3 | Android phone | |

## Nyomtató

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.120 | printer.local | nyomtató | |

## Szórakoztató elektronika

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.230 | — | smart TV | MAC: 02:00:00:aa:00:04, HTTP server, HTTP+HTTPS |

## Okosotthon eszközök

| IP | Hostnév | Típus | Megjegyzés |
|----|---------|-------|------------|
| 192.168.1.218 | vacuum.local | robot vacuum | |

## Gree klímák

Protokoll: Gree LAN v2, UDP port 7000, AES-128-ECB titkosítás  
WiFi modul: szabványos  
Firmware kulcs (discovery): `a3K8Bx%2r8Y7#xDh`

| IP | MAC | Firmware | Megjegyzés |
|----|-----|----------|------------|
| 192.168.1.215 | 02:00:00:aa:00:05 | V2.0.0 | |
| 192.168.1.221 | 02:00:00:aa:00:06 | V2.0.0 | |
| 192.168.1.241 | 02:00:00:aa:00:07 | V2.0.0 | |
