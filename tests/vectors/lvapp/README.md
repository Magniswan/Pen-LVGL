# LVAPP cryptographic test vector

The PEM files contain RFC 8032 test vector 1, a globally published test key.
They are not production secrets. Its key ID
`06e3fd8fda29bb60ab59557de61edb0a` is permanently classified as a test key and
must be denied by every production trust store.

`signed.lvapp` is generated from `app.json` and `root/` with the repository's
locked LVAPP tool. Native and Node tests consume the same bytes so package
layout, hashes, key IDs, and Ed25519 verification remain interoperable.
