# NetMon

A native C++ Windows tray application and background service for reviewing outgoing connection activity. This is an unsigned local preview, built for Windows 10 22H2 x64 and designed to use the same APIs on Windows 11. Windows 11 certification and the complete PLAN.md release matrix remain open.

## Changes in 0.5.0

The main window now has a native dark dashboard, sidebar navigation, summary cards, clearer connection rows and a dedicated details panel. **Use light theme / Use dark theme** switches appearance immediately. The preference is saved per user in `%LOCALAPPDATA%\NetMon\ui-preferences.json`; it does not change monitoring rules. Settings uses the same palette. Windows-owned scrollbars, message boxes and file dialogs may retain their system appearance.

**Destination map** displays the latest 200 matching recent observations using offline country estimates. Click a country marker or choose a country to inspect its distinct destination IPs, process paths and ports. Marker colors reflect the most severe classification in that country. Drag to pan, scroll or use +/- to zoom, and use Reset to return to the world view. Keyboard left/right arrows on the map move between countries.

Markers sit at representative country label points, **not exact IP/server coordinates**. Local and unlocated destinations are counted separately and are not plotted. Search applies to the map's recent records; the map is not a complete historical connection inventory. Country estimates do not change trust or alert rules.

Country lookup and grouping run on the existing background view worker. The map uses native GDI+ and a cached basemap; no web frame, browser runtime, online tiles or external geolocation requests are used. The simplified Natural Earth asset adds about 206 KiB. Open **More actions > Monitoring status** for collector, reputation-feed and storage details. Other import/export and alert actions are also under More actions.

## Changes in 0.4.0

The tray app now uses separate workers for service change notifications, view preparation, and ordered actions/import/export. The window receives immutable display snapshots through a bounded, coalescing mailbox. Superseded views are cancelled, automatic refreshes are capped at ten per second, and table queries pause while the window is hidden or minimized. Cell-only changes preserve native selection state and repaint only changed visible rows. Structural refreshes avoid unnecessary native count/selection resets and visibility toggles.

The service separates capture/enrichment from storage and publishes immutable snapshots for recent activity, review, status, and configuration. Four authenticated pipe sessions allow live reads to proceed while a history query or action is waiting. History searches cancel when their client disconnects; accepted changes retain ordered completion semantics. A saved Good/Bad decision refreshes the current review view without rewriting historical records.

Local responsiveness diagnostics run inside the installed app. They record message/handler timings and bounded stall samples, without collecting input contents or connection records. The current and previous run are saved as `%LOCALAPPDATA%\NetMon\ui-diagnostics.json` and `ui-diagnostics.previous.json`, each capped at 64 KiB. No diagnostics are uploaded.

The new concurrency tests deliberately stall queries and actions, supersede requests, and flood refreshes. Run `scripts/test-ui-responsiveness.ps1 -Stress` for the full-window stress check, or `-Live` for a read-only check against the installed service. See docs/VALIDATION.md for measurements and the remaining distinction between automated control timings and physical pointer latency.

## Changes in 0.3.1

The main view selector now uses direct tabs for Recent activity, Needs review, history, alerts, and rules. On the development PC the old dropdown took about 187 ms to open; the full-window regression measured new view actions below 10 ms and row selections below 3 ms. These are internal control timings, not a claim about physical mouse-to-display latency.

Tray icon updates and alerts now use a separate bounded worker, so calls into Explorer cannot stall the window or the data worker. Unchanged tooltip updates are skipped. View requests take priority over routine status/configuration polling, and superseded polling work is abandoned between requests.

Run `scripts/test-ui-responsiveness.ps1 -Live` for a 15-second, read-only test against the installed service. Without `-Live`, it starts an isolated demo backend. The test briefly opens its own window, switches views, selects rows, exercises map and theme actions, and measures the message queue and painting; it never marks connections, changes settings, or consumes real alerts.

## Changes in 0.3.0

The review table now uses virtual rows with cached display/detail text prepared on a background worker. Selecting a row does not rebuild the list or perform network/database work. Identical refreshes are skipped, selected identity and scroll position are preserved, and stale results from a previous view/search are discarded. View changes refresh immediately; typing in search is debounced for 150 ms.

Settings adds **Hide local-network destinations from Needs review**. It covers localhost, private IPv4, IPv6 unique-local/legacy site-local, shared address space, link-local, multicast, unspecified addresses, limited broadcasts, and directed IPv4 broadcasts for active adapter prefixes. Prefixes refresh in the background every 30 seconds. Public addresses ending in .255 are not automatically treated as broadcasts. The existing localhost-only option remains available.

The setting only filters Needs review. It does not alter alerts, history retention, trust decisions, or firewall behavior.

## Changes in 0.2.0

The Settings dropdown now uses its own control ID and no longer triggers Save. Needs review supports a localhost toggle (IPv4, IPv6, and IPv4-mapped IPv6) and a persistent list of exact application paths. Select a row and choose **Hide app from review**, or edit/remove paths in Settings. These filters affect only Needs review: alerts, saved history, and Recent activity remain available. They require no executable hash and do not grant Good trust.

The **Country (est.)** column uses a bundled DB-IP country database, November 2026, for offline IPv4/IPv6 lookups. Localhost, LAN, and reserved addresses receive descriptive labels. No monitored address is sent to an external service. The country is an estimate, especially for VPNs, CDNs, and anycast services; it is not proof of the operator's identity or physical server location, and does not change alert classification.

IP Geolocation by [DB-IP](https://db-ip.com), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). See data/README.md for provenance/checksums. This fixed database adds about 31 MiB to installation and is loaded only by the tray app's background worker. The capture service does not load it. Database updates are delivered with an app update.

Process details now explain why a hash/verified identity could not be obtained, rather than only showing unavailable.

## Run

Use the packaged directory in dist/NetMon.

- Run **Install.cmd** once and accept Windows' administrator prompt. It installs the service, enables the WFP Filtering Platform Connection audit subcategory (success and failure), and registers the current user's unelevated tray app at logon.
- Open **NetMon** from the Start menu. Closing its window leaves the tray and service active.
- Run **Demo.cmd** to try synthetic records without changing audit policy. Demo data is stored separately in LocalAppData/NetMon-Demo and the demo backend exits after ten minutes.
- Run **Uninstall.cmd** to stop/remove the service and tray task. Original audit settings are restored only if they still match NetMon's saved change. History and rules are retained in ProgramData/NetMon.
- No firewall allow/block rules are installed. Security-event-log retention is not modified.

The installer needs elevation because the Security log, audit policy, Program Files, and service manager are protected. The tray app itself runs without elevation. Installation results are written to ProgramData/NetMon/install-result.txt.

## Features

- WFP Security event subscriptions for outgoing policy-permitted (5156) and policy-blocked (5157) observations.
- Process path and PID from the source event. Live process-lifetime verification, SHA-256 file identity, and offline Authenticode verification when available.
- IPv4/IPv6 destinations, protocol and ports, timestamps, and explicit reasons.
- Balanced, Ask about new connections, and Threats/marked Bad only alert modes.
- Good/Bad rules scoped to an app/endpoint/port, a whole application, or a destination/port across apps. Good app rules require a verified hash. Changes do not silently inherit trust.
- Persistent rules, reset/delete, validated import/export, and exact matching with Bad precedence.
- Five-minute configurable alert cooldown and one-hour notification snooze.
- Suspicious-only or all-observed-event retention. SQLite history rotates within a configurable 32-4096 MiB budget (default 250 MiB), including reserved space for journals.
- A 30-day searchable history window and cleanup of expired closed segments. Searchable/paged saved history and alert views.
- A 200-record in-memory Recent activity view; ordinary rows disappear when not retained. Needs review filters this bounded recent view.
- User-supplied, expiring IP threat indicators. No reputation feed is enabled by default; the UI says so.
- Local named-pipe IPC restricted to the installing user, administrators, and SYSTEM. The client verifies the server PID and protected executable configuration through Windows Service Control Manager.
- Source, audit-policy, queue-loss, disk-use, and storage-error status. Local-only processing; no cloud requests.

## Semantics and current boundaries

"Policy-permitted" means WFP permitted the operation. It does **not** mean a TCP handshake completed or data reached a server. This preview does not add network ETW or infer established/closed connections. UDP/QUIC records are authorization observations, not byte counts. Source auditing can coalesce activity and miss events under load; universal coverage has not been certified.

Process identity may be unavailable after a process exits or when access is denied. A current process is rejected for enrichment if it started after the source event or its path differs. File hashes identify the inspected on-disk file, not a forensic image of the running process. Catalog-only signed files may be reported as unsigned/unverified; no blanket trust is granted by signature.

Destinations are IP addresses. Automatic DNS-to-process correlation, publisher names, automatic threat-feed downloads, ETW process lifecycle capture, richer historical grouping, and AI review are follow-up work. The current UI uses Windows-native controls and local-time display.

Security audit generation is machine-wide and can increase the Windows Security log's write rate even with suspicious-only app logging. The app's quota controls its history directory, not Windows-owned logs. The history database reserves up to 16 MiB plus headers for active growth/rollback; usable event storage is therefore below the chosen cap. Configuration is limited to 1 MiB and the remembered-connection cache to 8192 hashed keys. The latter is not a permanent trust decision: only saved Good/Bad rules establish trust.

History is ordered by segment recency, then event time within each segment. Exports contain only the displayed page (up to 200 recent or 100 historical records); they are explicit user-created files outside automatic retention.

Monitoring begins when the service subscription becomes active. A retained bookmark permits replay while its source records remain available. Source rollovers/outages are reported; replayed historical events are not shown as fresh notifications. Normal activity omitted by retention cannot be reconstructed for future AI analysis.

## Build and test

Prerequisites: Visual Studio 2019 16.11 or Visual Studio 2022 with desktop C++ tools, CMake, and a Windows 10 SDK. The VS2019 preset uses the 10.0.15063 SDK installed in the development test environment; the runtime baseline remains Windows 10 22H2. Use the vs2022 preset for a newer SDK.

~~~powershell
.\scripts\build.ps1
# Or:
.\scripts\build.ps1 -Preset vs2022
~~~

The script builds x64 Release, runs tests, and creates dist/NetMon. The VC runtime is statically linked. Windows' winsqlite3.dll is the database runtime; no SQLite driver or downloaded binary is bundled. nlohmann/json 3.12.0 is vendored with its license and verified upstream SHA-256.

Tests cover rule scope/precedence, fresh versus expired indicators, executable changes, unknown identities, IPv6 normalization, rejected settings, duplicate records, history rotation and budget, paging/search, process lifetime checks, and persistent decisions.

Use netmon_service.exe --probe for local prerequisites. For debugging, an elevated --console mode uses the installed owner/configuration. --demo --data <isolated-directory> --seconds 60 provides synthetic records; --demo-request accepts a JSON request for integration testing. Do not run two collectors against the same data directory.

See docs/ARCHITECTURE.md, docs/VALIDATION.md, and PLAN.md. Version 1.1 AI review remains opt-in roadmap work; no model integration or external telemetry is present.

To render and exercise the native dashboard against synthetic records, run `build\Release\netmon_ui_preview.exe`. It writes dark/light/compact/Settings PNGs into `build\ui-preview` and checks country selection, refresh stability, keyboard selection, theme preservation and the Settings dropdown without modifying the installed configuration.

## Validation

Installation, upgrades, standard-user IPC, all four IPv4/IPv6 TCP/UDP attribution checks, core, virtual-table, tray-worker, asynchronous-client and diagnostic checks, and eight demo integration checks have passed. A short loopback load test is recorded in docs/VALIDATION.md with raw results. Full-system overhead and Windows 11 compatibility still require separate validation.

## Sharing

The preview binaries are not Authenticode signed. Public release still requires an application license choice, publisher signing, Windows 11 testing, sustained real-traffic performance/coverage results, and completion of the remaining plan gates. Do not describe this preview as certified or guaranteed to detect malware.

Third-party notices are in THIRD_PARTY_NOTICES.md. Source can be reviewed locally without installing the service.

