# Issue 009 — Gree: AES-128-GCM support for firmware v1.23+

**Type**: feature  
**Priority**: low  
**Component**: `libappliances/src/infrastructure/gree_client`

## Summary

Gree firmware v1.23+ switched from AES-128-ECB to AES-128-GCM for pack encryption.
The current implementation uses ECB and will silently fail (garbage decrypt) against
newer firmware. Add GCM support with automatic detection.

## Detection strategy

1. Attempt ECB decrypt of the response pack.
2. If the result is not valid JSON (does not start with `{`), retry with GCM.
3. Cache the detected encryption mode per device in the persistent store.

## GCM packet layout (encrypted pack field, base64-decoded)

```
Bytes  0–11   Nonce / IV (12 bytes)
Bytes 12–27   Authentication tag (16 bytes)
Bytes 28+     Ciphertext
```

Total overhead per packet: 28 bytes before ciphertext.

## Key

Same 16-byte device key obtained during bind. GCM does not require a separate key.

## OpenSSL EVP

```c
EVP_aes_128_gcm()
EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL)
EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL)
EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce)
EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag)
/* EVP_DecryptFinal_ex returns 0 on tag mismatch */
```

## Persistent store change

Add a mode field to `~/.config/home-appliances/gree_devices`:

```
<ip> <mac> <device_key> <enc_mode>
```

`enc_mode`: `ecb` or `gcm`. Omitting = assume `ecb` (backwards compatible).

## Acceptance criteria

- ECB devices continue to work without change.
- GCM devices: first command auto-detects, caches mode, subsequent calls use GCM.
- Tag authentication failure logged as error (don't silently accept corrupt data).
- `ac list` shows encryption mode in verbose output.

## References

- [greeclimate GCM PR #92](https://github.com/cmroche/greeclimate/pull/92)
- [openHAB Gree binding GCM notes](https://www.openhab.org/addons/bindings/gree/)
- `docs/gree-protocol.md` — "Encryption compatibility"
