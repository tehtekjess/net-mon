# Offline IP country data

IP Geolocation by DB-IP: https://db-ip.com
Source: https://db-ip.com/db/download/ip-to-country-lite
Release: November 2026
License: Creative Commons Attribution 4.0 International
https://creativecommons.org/licenses/by/4.0/

geoip-country.csv is the unmodified DB-IP IP to Country Lite CSV database.
732,796 ranges; 32,485,911 bytes.
Upstream SHA-1: e2061015c43ec91c94a12191a6a613279eb94c0c
SHA-256: 5e968a64a29b973f794c5abc46d0711a0ba87c8a16c411e1cd70312db7a5d4e1

The app performs lookups locally. It does not send monitored addresses to DB-IP.
Country results are estimates and do not establish an operator's identity or the physical location of an anycast/CDN server. Nonlocal addresses are not inherently suspicious.

## Offline world geometry

world-map.json derives from Natural Earth 5.1.2, 1:50m Admin 0 country polygons:
https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/ne_50m_admin_0_countries.geojson

Natural Earth data is public domain:
https://www.naturalearthdata.com/about/terms-of-use/

Source SHA-256: 3e458fc036ad0a66411f2c1e6cac49c5d7bfb81cb1123bc513b22511a2b7fdeb
Bundled SHA-256: 133454c7c9fede4a1e7310dc73f97117bfd7cd54ca6dfdc5046a4faa7450fa76
241 shapes; 11,487 vertices; 211,346 bytes.

Reproduce with Python 3: download the pinned source explicitly, then run
`python scripts/build-map.py path/to/ne_50m_admin_0_countries.geojson`.
The script rejects a different source checksum. It simplifies exterior rings with
a 0.16-degree tolerance, omits Antarctica and interior holes, and retains country
label coordinates. ISO_A2_EH supplies country keys, with XK for Kosovo and no key
for unmapped shapes. The output uses equirectangular longitude/latitude geometry.

Markers use these country label points, not actual IP coordinates. Country
boundaries/names are cartographic display data and do not imply a political
position. The application never downloads map tiles or transmits monitored IPs.
