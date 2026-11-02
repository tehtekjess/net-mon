# Validation record

Test platform: Windows 10 22H2 x64.
Toolchain: Visual Studio 2019 16.11, MSVC 19.29, Windows SDK 10.0.15063.
Project sources compile with C++20, /W4 /WX /permissive- /sdl.

## Completed checks

- Original 0.1 validation: 44 automated checks: rule precedence and scopes, repeated unreviewed alerts, executable changes, unavailable identities, expiring indicators, IPv6, rejected/wrapping numeric input, configuration persistence, duplicate records, paging/search, process lifetime and lowercase WFP device paths.
- Storage rotation exercised with 6,000 large records and a 32 MiB cap. Newest records survived restart; oldest segments were evicted within budget.
- Eight end-to-end demo IPC checks: verified live-process hashing, query, persistent Good/Bad replacement, rule deletion, and pipeline health.
- Elevated installation and subsequent upgrades in the test environment. LocalSystem service and an unelevated per-user tray task are installed.
- Standard-user clients verify the pipe owner against the service manager's PID and protected executable configuration.
- Live WFP IPv4 TCP, IPv6 TCP, IPv4 UDP, and IPv6 UDP observations passed: exact generating PID, executable path, destination address/port, protocol, verified process lifetime, and SHA-256 availability.
- Subscription replay after service replacement, followed by new observations, passed on the installed build.
- Repeatable local load test and raw results are described below.

Live testing exposed and corrected the initial-subscription bookmark argument, initial replay signaling, Windows 10 empty-result polling, and case-insensitive device-path conversion. Synthetic tests alone did not establish these properties.

## Local load measurement

See load-validation.json for the final measured result and live-validation.json for live attribution checks. The script generates approximately 100 IPv4 loopback TCP connections per second for 30 seconds, using the default Balanced/suspicious-only settings, then waits three seconds for processing.

Final result: 3,000 generated connections; 3,072 received and processed observations; zero reported queue drops or collector errors; an empty queue at completion. Service CPU was 625 ms over 33.037 seconds (1.892% of one logical core), private memory 4,161,536 bytes, and working set 12,804,096 bytes.

The reported incoming event count also includes ambient outgoing activity, so it is not an exact one-for-one coverage measurement. CPU figures measure only the NetMon service, as a percentage of one logical core; they exclude the tray, Windows Event Log service, and the traffic generator. This short check is not a sustained whole-system benchmark.

Reproduce from the source checkout after installation:

~~~powershell
.\scripts\test-demo.ps1
.\packaging\Test-Live.ps1 -ServiceExe 'C:\Program Files\NetMon\netmon_service.exe'
.\scripts\test-load.ps1 -Seconds 30 -Rate 100 -OutputPath .\build\load-validation.json
~~~

The load script uses loopback only and does not change configuration or firewall rules.

## 0.2.0 regression checks

75 checks passed on the Windows 10 development host. Added tests cover loopback variants, LAN visibility, path normalization/deduplication, removal and restart persistence, preserving Bad alerts and saved history, country range boundaries/gaps, IPv6 and mapped IPv4, malformed overlapping data, and loading all bundled country ranges. The original 0.1 load measurements above describe the capture service, not the additional country database held by the 0.2 tray process.

The Settings issue was caused by using control ID 1 for both the alert dropdown and IDOK. The log checkbox also reused IDCANCEL (2). Controls now use distinct IDs above 1000, and Save/Cancel handlers accept button-click notifications only.

Installed 0.2.0 validation also passed all four IPv4/IPv6 TCP/UDP attribution checks. Localhost and application exclusions were saved, and live Needs review results contained zero matching rows. See validation-0.2.json for the installed-version record.

Accessibility inspection confirmed the country column and actual country/local-address labels. Screenshot capture timed out, so automated pointer interaction with the Settings dropdown could not be completed. The control-ID/notification routing fix is present in the installed executable; this is not a claim of an independently completed click-through test.

A one-time sample after loading the country database showed about 51 MiB of private memory for the tray and 4.6 MiB for the capture service. This is a point sample, not a sustained performance result.

## 0.3.0 regression checks

116 core checks pass, including local-network category boundaries, IPv4-mapped IPv6, directed broadcasts, /31 and /32 exceptions, default migration, persistence, and continued Bad alerts/history. An additional native control test instantiates the actual virtual table in its own hidden test window. It checks selected identity, scroll anchoring, unchanged refresh suppression, display callbacks, expired selection, and repeated selection/detail updates.

The final 20-check native-table run measured 0.2483 ms at p95 and 0.495 ms maximum across 1,000 selections. See table-validation-0.3.json. These are component timings in a hidden native window; they exclude desktop painting/input delivery and are not a claim about end-to-end visible click latency.

Installed 0.3.0 also passed the four live IPv4/IPv6 TCP/UDP checks. The local-network option was enabled for this test; live Needs review inspection found zero matching local-address rows, with zero reported collector errors/drops and successful adapter-prefix discovery. See validation-0.3.json.

## 0.3.1 responsiveness regression

The previous hidden-table test did not cover the delay reported in the full application. A new harness compiles the actual production window handlers and workers, opens its own visible window, and alternates view changes and row selections using read-only live service data. It does not inject physical mouse input, consume notifications, or alter configuration.

Before the fix, opening the main native dropdown took 186.8 ms maximum (186.2 ms p95). Posted messages waited up to 188 ms; this baseline used the coarse Windows tick counter. See ui-baseline-0.3.0.json.

After replacing the dropdown with direct tabs and isolating shell calls, the final high-resolution run recorded:

- 15 view changes: 9.24 ms maximum control-action time; 7.09 ms p95.
- 44 row selections: 2.52 ms maximum; 2.28 ms p95.
- 477 posted messages: 4.75 ms maximum queue latency; 0.126 ms p95.
- Asynchronous view data arrival: 15.87 ms p95; 231.92 ms maximum, including initial country-data startup.
- Zero service request errors. No individual control action or message-queue sample exceeded the test's 100 ms limit.

These measurements include the running window's control handlers and separately timed painting, but not end-to-end physical pointer delivery or desktop composition. See ui-validation-0.3.1.json. Screenshot capture still timed out in computer-use inspection; visual click-through was not independently verified.

The three CTest targets pass: 116 core checks, 20 table checks, and 14 tray-worker checks. The latter holds the shell transport blocked while enqueueing 2,000 updates, then verifies coalescing, delivery of the latest tooltip/alert, thread separation, unchanged-tooltip suppression, restart recovery, failed-add recovery, and shutdown cleanup.

Reproduce the full-window check with `scripts/test-ui-responsiveness.ps1 -Live`. Omitting `-Live` creates a separate synthetic backend. The complete-window harness is an explicit interactive test rather than part of unattended CTest.

## 0.4.0 event-driven responsiveness refactor

The final five CTest suites pass: 130 core checks, 25 native-table checks, 14 tray-worker checks, 45 asynchronous-client checks and four diagnostic checks. They cover independent completion while queries/actions are stalled, cancellation and stale replies, ordered actions, a 32-action bound including undelivered acknowledgements, 1,000 coalesced view requests, service snapshot reads during configuration writes, watch revisions/shutdown, and bounded diagnostic output. The client test replaced a cancelled view in 15.97 ms and stopped in 0.82 ms. See client-validation-0.4.json.

The isolated IPC test held three of four pipe sessions open without sending a frame; a fourth authenticated status request completed in 12.82 ms. Change notification and saved configuration acknowledgement also passed. See events-validation-0.4.json. All eight demo integration checks passed.

The final delayed-query/rapid-refresh window test completed 15 view changes and 44 selections. Its maximum view-switch action was 5.21 ms, row selection 2.20 ms and posted-message delay 4.75 ms; four obsolete queries were cancelled. Initial/asynchronous data arrival is measured separately (256.55 ms maximum, 38.91 ms p95). See ui-stress-0.4.json. A separate final full-window test against the installed service measured 15 view switches at 5.77 ms maximum, 45 selections at 2.06 ms maximum, and 480 posted messages at 6.74 ms maximum queue delay. Data arrival was 24.41 ms p95, 229.00 ms maximum including initial loading. See ui-validation-0.4.json. These exercise production controls and worker code, not physical pointer-to-display timing.

A desktop inspection experiment exposed a separate measurement interaction: after attaching the computer-use accessibility inspector, the installed UI thread reached roughly 90% of one core and heartbeat delays exceeded three seconds. Stack samples were dominated by Windows UI Automation/native-control processing, including work inside GetMessage rather than application dispatch handlers. Removing unnecessary native count/selection resets, redraw visibility toggles and repeated status text improved the internal window test, but did not by itself eliminate the inspector-induced workload. Detaching/resetting the inspection session and allowing its backlog to drain returned the visible app to approximately 1.25% of one core in a ten-second sample. This interaction remains a compatibility investigation; do not claim responsiveness with arbitrary accessibility clients. No accessibility features are disabled.

The final installed load check ran with the window open on the interactive desktop and the inspector detached. It generated 3,000 local IPv4 TCP connections in 30 seconds. The service received/processed 3,050 observations including other machine activity, reported zero drops/errors, and drained its queue. Over the 33.048-second observation interval, service CPU averaged 1.041% of one core and the tray averaged 3.536%. Private memory was 6.25 MiB for the service and 39.63 MiB for the tray. These figures exclude Windows Security-log and other system overhead and use the existing suspicious-only retention settings; they are a short local-load result, not a sustained coverage guarantee. See load-validation-0.4.json.

Both final installed binaries match the staged package. Installation preserved the complete configuration, including rules and local-network/application exclusions, and all four live IPv4/IPv6 TCP/UDP attribution checks passed again. See validation-0.4.json.

The installed app now records real input age, handler duration, posted-heartbeat delay and bounded wait-chain samples in LocalAppData/NetMon/ui-diagnostics.json, preserving the previous run. The four diagnostic tests verify measurement/bounds/privacy behavior with deliberately delayed synthetic messages; they do not prove end-to-end user-perceived latency. The desktop inspector still cannot complete a physical click check because it reports unavailable input geometry. Remaining intermittent user-perceived lag should be assessed against these production diagnostics, without assuming that fast internal-control tests prove it is gone.

## 0.5.0 dashboard and offline map

All six CTest suites passed on the Windows 10 development host: 130 core, 25 native-table, 14 tray-worker, 45 asynchronous-client, four diagnostics and 21 map-model checks. Map checks verify country grouping, shared-IP process/port attribution, worst classification, IPv6, local/reserved exclusion, missing geometry and country representative points.

The native preview executable rendered seven production-control states using synthetic records: dark/light connection and map views, compact connection/map layouts and dark Settings. The rendered client areas were visually inspected for contrast and clipping. Twelve additional checks exercised country selection, stable open dropdowns during refresh, IP selection across refresh, keyboard country selection, light-theme preservation and the Settings dropdown staying open. Zoom, pan and Reset code paths also ran. This does not replace physical mouse, screen-reader or multi-monitor DPI validation.

The final delayed-query/rapid-refresh harness includes map navigation, zoom and appearance changes. Maximum internal action times: view switch 21.67 ms, row selection 1.24 ms, country selection 0.84 ms, zoom 1.23 ms and theme switch 34.31 ms. Painting is measured separately; maximum full theme repaint was 53.30 ms and maximum posted-message delay was 58.70 ms. Two obsolete queries were cancelled. Asynchronous view arrival was 57.04 ms p95 and 288.25 ms maximum, including initial loading. See ui-stress-0.5.json. These are native control/worker measurements, not physical pointer-to-display timing.

The final installed-service UI run passed 15 view changes and 29 row selections, plus five country/zoom actions and four theme actions. Maximum action times were 20.46 ms for a view switch, 1.29 ms for row selection and 24.03 ms for theme switching. Posted-message delay was 5.77 ms p95 and 70.23 ms maximum. The full theme repaint peaked at 63.05 ms; view data arrival was 38.42 ms p95 and 274.05 ms maximum including startup. See ui-validation-0.5.json.

Installation preserved the complete configuration and all four live IPv4/IPv6 TCP/UDP attribution checks passed. Installed executables and both offline data files match the staged package. See validation-0.5.json.

The first installed 0.5.0 load sample, after opening the window through the computer-use helper, showed 90.76% of one core for the tray. Although no explicit accessibility-tree request was made in that run, resetting the helper immediately reduced a ten-second idle sample to 0.94%. Production diagnostics had a 5.25-second heartbeat stall while no application handler was active. A subsequent 100-sample stack profile spent 98 samples idle in GetMessage. This reproduces the previously observed helper interaction; it does not establish compatibility with all accessibility clients. The affected result is retained in load-inspection-0.5.json.

The final load check left the installed Recent activity window visible with the computer-use helper reset. It generated 2,999 local TCP connections in 30 seconds and received/processed 3,036 observations including ambient traffic. There were zero reported drops or collector errors, no storage error and an empty queue at completion. Over 33.039 seconds the service used 1.417% and the tray 2.412% of one core. Private memory was 7.04 MiB for the service and 30.35 MiB for the tray. These figures exclude system-wide Security-log overhead and are a short local-load sample, not a sustained performance guarantee. See load-validation-0.5.json.

## Remaining release validation

The native tray executable was built, launched, and installed. The 0.5.0 client-area renders were inspected using synthetic records in the app\'s own test process. Installed desktop screenshot capture and physical notification interaction remain unverified; the earlier computer-use accessibility inspection could not complete and introduced measurement overhead.

Before a public release:
- Windows 11, Secure Boot/Memory Integrity, VPN/proxy and multi-user matrix.
- Blocked connections, short-lived process coverage, source rollover/clearing, and loss accounting under hostile/extreme load.
- Longer sustained 100 connections/second runs, bursts, full-system CPU/disk/network baseline, and 24-hour soak.
- Installer failure rollback and ownership-aware uninstall tests.
- Signing, application license selection, and remaining PLAN.md product/release gates.

The app is an unsigned local preview. These checks do not certify universal coverage or malware detection.

