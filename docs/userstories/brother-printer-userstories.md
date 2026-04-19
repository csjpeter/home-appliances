# Brother printer — user stories

## US-P01: Quick printer status at a glance

**As a** home user,  
**I want to** check whether the printer is ready with one command,  
**So that** I know before walking to the printer whether it will print.

### Acceptance criteria

- `printer status <ip>` prints: printer state, toner %, any active error reason.
- State shown as string: "idle", "printing", "stopped", "offline".
- Toner below 10 %: warning prefix `[!]`.
- Exit 0 on success, 1 if unreachable.

### Example output

```
State:  idle
Toner:  68%
Error:  none
```

---

## US-P02: Toner level monitoring

**As a** home user,  
**I want to** see the exact toner remaining percentage,  
**So that** I can order a new cartridge before running out mid-print.

### Acceptance criteria

- `printer toner <ip>` queries SNMP OID `1.3.6.1.2.1.43.11.1.1.9.1.1` (current)
  and `1.3.6.1.2.1.43.11.1.1.8.1.1` (max); prints `(current/max * 100)%`.
- If Brother proprietary OID `1.3.6.1.4.1.2435.2.3.9.1.1.2.10.1` returns low flag,
  print `[LOW]` suffix.
- Output usable in shell: `printer toner <ip> --raw` prints just the integer 0–100.

---

## US-P03: Page count for maintenance tracking

**As a** home user,  
**I want to** see the total pages printed,  
**So that** I can track when to perform maintenance or replace the drum.

### Acceptance criteria

- `printer pages <ip>` queries `1.3.6.1.2.1.43.10.2.1.4.1.1` and prints the count.
- If Brother proprietary OID `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11.0` is available,
  also print "pages until next maintenance: N".

---

## US-P04: Full consumables report

**As a** home user,  
**I want to** see toner, drum, and maintenance status in one command,  
**So that** I know which consumables are ageing before they fail.

### Acceptance criteria

- `printer consumables <ip>` prints a table:
  ```
  Toner:       68%
  Drum:        54%  (approx 12 400 pages left)
  Maintenance: OK   (next in 8 200 pages)
  ```
- Data from: SNMP standard MIB + Brother OIDs `5.5.8` (maintenance %) and `5.5.11` (pages).
- Warning `[!]` on items below 20%.

---

## US-P05: Error detection and paper jam notice

**As a** home user,  
**I want to** know immediately if the printer has a jam or cover open,  
**So that** I don't stand waiting at a printer that will never start.

### Acceptance criteria

- `printer status <ip>` includes error reason if `printer-state-reasons` (IPP)
  or `hrPrinterStatus` (SNMP) indicates an error.
- Specific conditions detected: paper jam, toner low/empty, door open, paper out.
- Exit code 2 if printer is in error state (to support shell scripting).

---

## US-P06: Pre-print readiness check in a shell script

**As a** developer / power user,  
**I want to** use the printer status in a shell script before submitting a print job,  
**So that** automated printing workflows can detect and report problems early.

### Acceptance criteria

- `printer status <ip> --quiet` exits 0 if idle/ready, 1 if offline, 2 if error.
- No output in `--quiet` mode.
- Usable as: `printer status 192.168.50.x --quiet && lp myfile.pdf`

---

## US-P07: Probe printer reachability

**As a** home user,  
**I want to** check if the printer is on the network,  
**So that** I can troubleshoot connectivity before blaming print jobs.

### Acceptance criteria

- `printer probe <ip>` attempts SNMP query (sysDescr OID `1.3.6.1.2.1.1.1.0`).
- Prints "online — Brother <model>" or "offline (unreachable)".
- Timeout: 2 s. Exit 0 / 1.
