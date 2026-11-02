# net-mon implementation plan

Status: an unsigned Windows 10 preview is implemented, installed in the test environment, and live-tested. The full 1.0 release gates below remain the roadmap. See README.md and docs/VALIDATION.md for implemented behavior, measured checks, and outstanding release gates.

Build a professional, efficient C++ desktop application that observes outgoing network connections, identifies their processes and destinations, explains suspicious activity, and remembers the user's Good/Bad decisions. Provide a Windows service for continuous monitoring and a tray application for interaction.

## 1. Platform and release requirements

- Initial acceptance testing uses a Windows 10 22H2 x64 test environment.
- Set the initial supported baseline to Windows 10 22H2 x64 and test Windows 11 x64 as an equal release requirement. Verify the test environment's architecture during implementation setup; do not infer it from the OS edition.
- Windows 10 Home must work without Group Policy Editor or enterprise-only features.
- Build against APIs available on the baseline OS. Resolve optional newer APIs at runtime and test missing-feature paths.
- Treat Windows 11 compatibility as a tested requirement, including Secure Boot and Memory Integrity where supported. Never require disabling platform security to run a release build.
- Older Windows 10 releases, Windows Server, x86, and ARM64 are future compatibility work.
- Keep the application's compatibility policy distinct from Microsoft's operating-system servicing lifecycle.
- Version 1.0 monitors and alerts. Good/Bad decisions control notifications; they do not change firewall policy or block connections.

## 2. Collector and signing decision

The recommended first implementation is an ordinary user-mode Windows service using built-in Windows telemetry. A custom kernel driver is not a version 1.0 prerequisite.

Prototype the native collector first. Measure coverage and total system overhead on Windows 10 and Windows 11 before committing to the production backend. If it cannot meet the gates below, evaluate an existing signed WinDivert backend through its public API before considering a custom driver.

### User mode versus kernel mode

WFP exposes user-mode management/event APIs, but custom callouts at the TCP/IP connection layers run in kernel mode. A UMDF user-mode driver cannot substitute for an ALE connection callout. Microsoft recommends using the built-in functionality and user-mode management APIs when they suffice. [WFP architecture](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-architecture-overview), [callout programming considerations](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/callout-driver-programming-considerations)

A normal service and tray executable do not need kernel-driver signing. Plan Authenticode signing for distributed executables and the installer to establish publisher identity. User-mode drivers are not universally subject to kernel-mode signing rules, but some driver package/install scenarios still require signatures; UMDF is not a blanket signing exemption. A custom kernel driver needs the appropriate Microsoft signing/distribution process. [Windows driver signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/windows-driver-signing-tutorial)

### Native collector: preferred starting point

- Consume outbound WFP Security audit events: 5156 for policy-permitted connections and 5157 for policy-blocked connections. These carry application, PID, endpoints, and protocol. A 5156 event is an authorization observation, not proof that a remote TCP handshake succeeded. [Event 5156](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/auditing/event-5156), [event 5157](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/auditing/event-5157)
- Use Windows Event Log subscriptions and bookmarks, not repeated command-line scraping or full-log polling. Handle stale bookmarks, channel clearing, rollover, replay, and duplicates explicitly. [Event subscriptions](https://learn.microsoft.com/en-us/windows/win32/wes/subscribing-to-events)
- Add ETW process lifecycle information for identity and short-lived-process correlation. Evaluate limited network ETW for connection outcomes and any measured coverage gaps. Parse event payload PIDs rather than assuming the ETW header identifies the owning process. TCP/IP and UDP/IP events have documented payload attribution. [TCP/IP ETW](https://learn.microsoft.com/en-us/windows/win32/etw/tcpip), [UDP/IP ETW](https://learn.microsoft.com/en-us/windows/win32/etw/udpip)
- ETW network tracing may produce packet-frequency events even if the consumer keeps only connection records. Include that producer cost in benchmarks; do not label it cheap solely because the app discards events.
- Direct WFP network-event subscriptions may supplement the collector, but do not assume they are a complete process-instance feed. The documented network-event header contains application identity and endpoints but no PID field. Validate specific API versions and event coverage on both OS families. [WFP event header](https://learn.microsoft.com/en-us/windows/win32/api/fwpmtypes/ns-fwpmtypes-fwpm_net_event_header3)
- Audit policy changes affect the machine. Setup must show the required changes, record prior state, respect externally managed policy, and report if policy is subsequently disabled. On uninstall, restore only settings still matching the app's own changes; preserve later administrator changes.
- Success auditing can generate substantial Security-log traffic. Measure Event Log service CPU, writes, retention pressure, and event loss as well as this app's resources. The app's suspicious-only setting does not reduce source audit generation. [Filtering Platform Connection auditing](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/auditing/audit-filtering-platform-connection)

### Existing drivers and Sysinternals

Sysmon can supply network events through its documented Event Log channel: Event 3 reports TCP/UDP endpoints and process identity, Event 1 supports process correlation, and Event 22 supplies DNS observations. Network events must be enabled. The current Sysmon v15.22 download page lists Windows 11 and higher as supported clients, so it is not a dependable Windows 10 baseline dependency. An installed older version would need separate version-specific validation. [Sysmon documentation and requirements](https://learn.microsoft.com/en-us/sysinternals/downloads/sysmon)

Do not design against private Sysmon, Process Monitor, or Process Explorer IOCTLs. The reviewed Sysinternals documentation does not establish a supported public IOCTL telemetry SDK for this purpose. Use documented outputs if an optional integration is added.

Do not bundle Sysinternals binaries with the app under an assumed redistribution right. The published terms restrict publishing copies and transfers; an optional integration should use a separately installed copy unless a suitable distribution agreement is obtained. Preserve existing Sysmon configurations and do not enable unrelated high-volume collection. [Sysinternals license terms](https://learn.microsoft.com/en-us/sysinternals/license-terms)

WinDivert is a better candidate for deliberately reusing a driver through a public interface. Its FLOW and SOCKET layers expose process IDs and connection/socket events, and official binary distributions include signed drivers. Evaluate both layers with SNIFF and RECV_ONLY flags for passive observation; avoid packet diversion and reinjection. It cannot retrospectively supply flows created before capture starts. Rebuilding its driver requires signing again. [WinDivert API and signing documentation](https://github.com/basil00/WinDivert/blob/master/doc/windivert.html)

WinDivert still installs a kernel driver and needs elevated setup/access. Pin a vetted release, verify its signature, and test Windows 10/11, Secure Boot, Memory Integrity, endpoint protection, coexistence, and removal. Its published license choices are LGPLv3 or GPLv2; fulfill the selected license obligations before redistribution. Do not assume an unchanged signed binary automatically loads under every machine's security policy. [WinDivert project and licensing](https://github.com/basil00/WinDivert), [official releases](https://github.com/basil00/WinDivert/releases)

If both native collection and the supported third-party option fail the coverage/performance gates, revisit a minimal signed WFP callout driver. Record the specific gap that justifies it, its distribution costs, and the extra test burden. Do not silently narrow coverage or introduce a private-driver dependency.

## 3. Monitoring contract

Observe host-originated outgoing connections across applications, services, users, and sessions. Include internet, LAN, loopback, IPv4, IPv6, TCP, and UDP including QUIC. Exercise ICMP, multicast, broadcast, and raw-socket traffic in the collector prototype and document their actual representation.

Record connection-level observations rather than every packet. For UDP, identify remote endpoint activity and flow lifetimes without claiming a TCP-style handshake. WFP's ALE connection layer sees TCP connect operations, first UDP sends to unique remote tuples, and certain outbound ICMP activity; flow establishment has protocol-dependent semantics. [ALE layer semantics](https://learn.microsoft.com/en-us/windows/win32/fwp/ale-layers)

Use explicit observation states: attempted, policy-permitted, established TCP connection, observed UDP activity, policy-blocked, closed, and outcome unknown. Keep observations and inferred outcomes distinguishable. A timeout, missing event, or service restart is not evidence of a firewall block.

The event model must preserve:

| Field group | Data |
| --- | --- |
| Identity | Stable event ID, source and source record ID, source version, schema version |
| Time | UTC event and ingestion times; monotonic timing for durations where available |
| Process | PID, process-instance key/start time or source GUID, executable path, user/session, package identity where available |
| Binary | File identity, hash, signer, and verification state; unknown values remain explicit |
| Network | Address family, source/destination addresses, ports or ICMP type/code, protocol, direction, interface/compartment where available |
| Interpretation | Observation type, outcome, correlation confidence, completeness flags |
| Decision | Matched rule ID/revision, classification, reasons, detector/feed versions |
| Enrichment | Candidate hostname, provenance, timestamp/expiry, confidence |

Associate records using process lifetime plus endpoints, protocol, source identifiers, and time. Do not merge separate short connections merely because their tuples match. Deduplicate reauthorization and replay without discarding real repeated connections.

Capture or correlate process identity promptly. If a process exits before it can be inspected, keep event-time identity and label missing enrichment. Never associate an old PID with a newly created process. Cache executable verification by file identity/version and invalidate on change.

IP and port are authoritative endpoint observations. DNS names are best-effort, time-bounded associations. Reverse DNS does not prove which hostname the app requested, and shared IPs must not inherit domain trust automatically. Encrypted/custom DNS, browser helpers, proxies, VPNs, and service hosts limit attribution; display the observed process/endpoint and uncertainty.

At startup, optionally inventory existing TCP connections using supported Windows APIs and label them as pre-existing with unknown start time. Do not present snapshots as recovered historical events. Monitoring starts when the collector becomes healthy; boot-before-start and outage gaps remain visible.

Guest processes inside VMs/WSL and forwarded traffic do not necessarily map to a host executable. Document this boundary. Publish a tested coverage matrix rather than an unconditional claim to capture every possible event.

## 4. Application architecture

~~~mermaid
flowchart LR
    C[Selected telemetry collector] --> Q[Bounded queue]
    Q --> N[Normalization and process correlation]
    N --> R[Rules and detectors]
    R --> H[Bounded history]
    R --> A[Alert manager]
    A --> T[Tray interface]
    T --> P[Authenticated local IPC]
    P --> R
    N --> E[Asynchronous enrichment]
    E --> R
~~~

- Service: owns collection, correlation, rules, storage, enrichment, and alert state. Starts automatically and remains active when the UI closes.
- Tray app: standard-user process for notifications, review, search, settings, and health. Supports Explorer restart and notification activation.
- Local IPC: versioned, bounded messages over an authenticated named pipe. Check the caller's SID/session and authorize read/write operations separately.
- Data access: restrict service data to the service identity and authorized users. Viewing other users' activity and changing machine-wide rules require appropriate authority.
- Core library: typed event model and deterministic rule evaluation independent of Windows APIs and the UI.
- Collector boundary: a small explicit interface for normalized observations and health. Implement one production backend after the prototype decision; avoid a general plugin framework.

Choose the least-privileged service identity that can access the selected sources. If LocalSystem is necessary, document why, restrict its IPC commands, and keep untrusted parsing and optional AI work away from that privilege boundary.

Failures must not affect ordinary connectivity. Never add blanket permit rules to obtain telemetry. Stop enrichment before allowing queues to grow without bound. Surface source disconnects, policy changes, lost events, and persistence failure.

## 5. Alert modes and suspicious activity

Logging and notifications are independent settings.

| Alert mode | Behavior |
| --- | --- |
| Balanced (default) | Alert on user-marked Bad, known threats, and sufficiently strong unusual-activity signals. Group other new activity for review. |
| Ask about new connections | Alert on previously unreviewed application/destination combinations, with grouping and cooldowns. |
| Threats and marked Bad only | Alert on validated threat matches and Bad rules; do not interrupt for novelty alone. |

Begin with transparent deterministic rules, not a model making unexplained verdicts. Version 1.0 signals include explicit Bad rules, fresh threat-indicator matches, executable identity changes, and combinations such as a new unsigned executable in a temporary directory contacting an unfamiliar destination.

A signature, a common publisher, an unfamiliar country, an unusual port, or a new destination alone is not a reliable verdict. Observe first-seen activity without automatically trusting it during a learning period.

Every alert shows process, destination, time, observation status, severity, and specific reasons. Distinguish confirmed user decisions, reputation evidence, and behavioral suspicion.

Threat feeds are configurable and cached locally. Before selecting a production feed, verify provenance, update method, redistribution terms, false-positive handling, and expiry. Display feed health and freshness. If no usable feed is enabled, label reputation checking unavailable; never display "no known threats" as if a check succeeded.

## 6. Good/Bad rules and notification behavior

| User action | Persisted effect |
| --- | --- |
| Mark Good | Suppress ordinary future alerts for the selected match |
| Mark Bad | Alert on future matches in every alert mode |
| Reset to Unknown | Remove that decision and resume configured evaluation |
| Snooze | Temporarily suppress notifications without changing classification |

The default match is application identity + exact destination IP + protocol + remote port. Do not match on PID or ephemeral source port. Rules also support deliberately selected whole-app and destination-wide scopes. Domain scope requires a reliable association; show its limitations and never turn one trusted hostname into trust for every tenant of a shared IP.

Present the exact scope before saving. For unsigned applications use a hash-bound identity by default. Signed-app rules may explicitly allow publisher/path continuity across updates; signer or unexpected binary changes require review. Signing alone never grants trust.

Evaluation precedence is explicit Bad, fresh high-confidence threat evidence, explicit Good for ordinary behavior, then configured heuristic/novelty evaluation. A newly detected threat can therefore alert despite an earlier Good decision; explain the override.

Rules have IDs, revisions, timestamps, scope, reason, optional expiry, and enabled state. Provide edit, delete, export, and validated import. Keep historical decisions attached to the rule/detector revision that produced them.

Separate classification from delivery. An alert cooldown never prevents matching events from being classified or retained under the selected logging policy. Default cooldown is five minutes per app/destination/reason, with repeat counts. Dismissal does not remove a Bad rule. Manual snooze suppresses popups but preserves alert history.

## 7. Logging, retention, and disk limits

| Setting | Default |
| --- | --- |
| Logging mode | Suspicious connections only |
| Alternative | All observed connection records, including Good |
| History budget | 250 MiB, configurable |
| Age retention | 30 days, configurable; whichever limit is reached first |
| Diagnostic budget | 10 MiB |
| Enrichment/baseline cache budget | 25 MiB |
| Configuration/rule store budget | 10 MiB; reject excess additions rather than silently remove rules |
| Alert cooldown | 5 minutes |

Evaluate every collected event regardless of retention mode. Store alert-triggering novelty events in Ask mode as review-needed records. Under Balanced mode, retain only bounded review summaries for ordinary new activity and disclose that behavior. History that was never stored cannot be reconstructed later.

Use SQLite for indexed history and a separate durable configuration database. Rotate bounded history segments and remove oldest segments. Include indexes, journals/WAL, migration workspace, and pending exports in storage planning. Reserve headroom before writes; avoid full-database maintenance that temporarily doubles disk usage. Set explicit quotas for alert summaries and repeat counts within the history budget.

Rules/settings survive history cleanup. Include a single bounded configuration backup. User-requested exports have a displayed size estimate and destination and are outside automatic history retention.

Show app-owned usage and the configured maximum separately from Windows-owned source logs. Security and Sysmon Event Logs have their own channel limits/retention and can still contain normal activity when this app keeps only suspicious events. Inspect and explain those settings; do not silently clear logs, enlarge them, enable unbounded archives, or overwrite enterprise policy.

Use one background writer, bounded batches, prepared statements, and paged queries. Detect database corruption, full disk, read-only storage, and failed rotations. Keep monitoring in bounded memory, show degraded health, and report any persistence losses. Do not repeatedly generate new databases/backups on failure.

## 8. Performance requirements

These are initial acceptance targets to measure, not claims about existing software. Establish a repeatable baseline in the test environment during the first milestone and publish actual results.

| Measure | Initial acceptance target |
| --- | --- |
| Idle overhead | Under 0.2 percentage points of total-machine CPU averaged over 10 minutes |
| Typical workload | Under 1 percentage point of additional total-machine CPU averaged over 30 minutes |
| Memory | Service private bytes <=100 MiB; hidden tray <=50 MiB after warm-up |
| Immediate alerts | p95 <=2 seconds from source event arrival for locally available rules; measure source delivery latency separately |
| Normal capture | No missing required events in a controlled 100-new-connections/second, 10-minute test |
| Burst behavior | Measure at 1,000 new connections/second for 30 seconds; stay bounded and report observed loss |
| Network impact | No repeatable throughput regression over 2%, and no measurable added network-path dependency on the service/UI |
| Long-running stability | No sustained memory/handle growth in a 24-hour soak |

Include Event Log/ETW production costs, any third-party service/driver, disk writes, and the UI in whole-system comparisons. Consumer-only CPU measurements are insufficient. Record hardware, power settings, event rates, and variance with results.

Use event subscriptions, bounded queues, fixed worker limits, asynchronous I/O, and cached enrichment. Cap the ingestion queue initially at 32 MiB and account for it in the service budget. Cache hashes/signatures by file identity, avoid reverse DNS on every event, and skip already-completed enrichment.

No synchronous network, hash, signature, or disk work in ingestion callbacks. Batch history writes (for example, 250 ms or 250 events, whichever comes first) and throttle visible UI refresh to at most four times per second. Persist critical rule changes promptly.

Do not enable broad file/image tracing or collect packet payloads. Keep the hot path proportional to connection activity where possible. Measure high-throughput long-lived traffic separately because packet-frequency ETW can violate that goal.

Under overload, prioritize collection/health and defer enrichment/UI work. Report app-level drops exactly and provider-level loss to the extent exposed. Missing upstream events are not always detectable; never claim zero loss merely because the local queue did not overflow.

## 9. Code quality and distribution

- Use C++20, MSVC, CMake presets, a Windows 10-compatible toolchain configuration, and pinned dependencies. Use Win32 common controls/tray APIs with C++ wrappers for a lightweight native UI.
- Separate collector, core model, rules, enrichment, storage, service, IPC, and UI responsibilities. Keep interfaces small and ownership explicit.
- Use RAII for handles, subscriptions, locks, transactions, and COM resources; smart pointers only where ownership calls for them.
- Use typed addresses, timestamps, process identities, and enums. Keep Windows UTF-16 conversion at boundaries and define serialization encodings.
- Enable /W4, /WX for project code, /permissive-, appropriate security/compiler checks, formatting, static analysis, and supported user-mode sanitizers.
- Bound all external strings, arrays, message sizes, and imported data. Use parameterized SQL, explicit timeouts/cancellation, and structured errors. Never allow exceptions across Windows callback boundaries.
- Document threading, shutdown order, error handling, schema migrations, rule precedence, and collector limitations. Comments explain constraints and reasoning.
- Add meaningful unit tests for rule precedence/scoping, PID reuse, deduplication, retention, and schema migration. Add parser/import fuzzing and integration tests for Windows-specific behavior.
- Include reproducible build instructions, dependency inventory/SBOM, licenses/notices, architecture decisions, contributor guidance, and a security-reporting contact before sharing.
- Select an application license before public release. If using WinDivert, comply with its license and distinguish upstream driver provenance from this project's binaries.
- Produce signed release executables/installers, checksums, release notes, and a dependency/update policy. Signing an application does not sign a new custom driver.
- Install/update/uninstall cleanly, preserve settings across upgrades, and remove only app-owned artifacts. Do not uninstall shared telemetry dependencies.
- Keep updates explicit in version 1.0. Any later automatic updater must authenticate packages and handle rollback safely.

Proposed source layout:

~~~text
src/core/
src/collectors/
src/rules/
src/enrichment/
src/storage/
src/service/
src/ipc/
src/ui/
tests/unit/
tests/integration/
tests/performance/
docs/
packaging/
~~~

## 10. Tray and review experience

The tray shows healthy, starting, degraded, or stopped state, with a menu for history, pending review, rules, alert mode, logging settings, and snooze. Closing the window leaves the service running; stopping monitoring is a separate explicit action.

The main window supports time, app, destination, protocol, severity, and decision filters; paged results; grouping of repeated activity; and CSV/JSON export. Details show original observations, enrichment provenance, matched rules, and Good/Bad controls.

Notifications open the relevant event or group. Preserve unread alerts when the tray is not running, handle Windows notification suppression, and support keyboard navigation, high DPI, dark/light themes, and accessible status text.

Provide an easy diagnostic view showing collector, coverage, policy/feed health, queue depth, event lag, storage usage, and known gaps. Do not expose implementation details in ordinary connection decisions.

## 11. Version 1.1: optional periodic AI review

Add AI only after version 1.0 collection and deterministic alerts are reliable. The useful role is finding patterns across observations and explaining them: repeated timing patterns, unexpected destination changes, or related activity across processes.

- Disabled by default; manual review plus configurable intervals, initially 30 minutes.
- Run outside the privileged service, preferably in a cancellable low-priority worker.
- Feed a bounded, incremental window of structured summaries and selected evidence, not the entire raw log on each run.
- Persist a review cursor and model/prompt version. Handle late events, retention gaps, retries, and downtime without repeatedly sending the same history.
- Use deterministic grouping/statistics first. A model interprets measured evidence rather than inventing traffic volumes or reputation facts.
- Return findings with event IDs, observed facts, an explanation, confidence/uncertainty, and suggested review actions. Validate structured output and reject references to nonexistent events.
- Treat all log fields and DNS/process strings as untrusted data. They must not become instructions or trigger tools/commands.
- AI never automatically changes Good/Bad rules, blocks traffic, executes commands, or silently dismisses deterministic alerts.
- Support a provider-neutral interface. Evaluate local and remote models separately; local inference may cost significant CPU/RAM, while remote review exposes selected metadata and has usage costs.
- Cloud review is an explicit opt-in with an exact data preview, redaction controls, and endpoint/provider disclosure. Store credentials using Windows-protected credential storage; never log secrets.
- Exclude usernames, command lines, and full paths by default. Allow stable pseudonyms where useful; explain when destination identifiers are necessary for the requested analysis.
- Bound requests, tokens, spend, wall time, CPU, memory, concurrency, and retries. Skip overlapping runs and back off on errors. Monitoring remains fully functional with AI disabled or unavailable.
- Group repeated AI findings; clearly label them as AI-assisted suggestions and show coverage and model limitations.

Data completeness is a separate setting: suspicious-only retention cannot support full behavioral review after normal events are discarded. Offer either review of retained suspicious events only, Log All, or explicit opt-in to a separate bounded all-activity summary store. Specify that store's fields, age/size cap, and visibility. Never silently retain normal connection history because AI was enabled.

Before shipping, evaluate detection quality, false positives, prompt-injection resistance, privacy controls, cost, and resource use against a labeled test corpus and deterministic baselines. Do not market AI review as guaranteed malware detection.

## 12. Implementation milestones and acceptance gates

| Milestone | Deliverables | Exit gate |
| --- | --- | --- |
| P0: collector feasibility | Small native collector, traffic test harness, Windows 10/11 coverage and performance report; signed third-party comparison only if needed | Select and record one backend that meets attribution, coverage, deployment, and overhead requirements |
| P1: service and core | Typed observations, process lifetime correlation, bounded queues, IPC, health, shutdown/recovery | Controlled traffic is attributed correctly; source outages and PID reuse do not produce false identities |
| P2: rules and storage | All three alert modes, Good/Bad/snooze, both logging modes, rotation, imports/exports | Rule precedence is deterministic; storage remains bounded including auxiliary files; decisions survive restart |
| P3: tray experience | Notifications, review/history/rules/settings, accessibility and multi-session handling | End-to-end decisions affect future notifications correctly; service continues with UI closed |
| P4: enrichment and release | DNS/reputation integration, installer, signing, documentation, dependency/legal review, stress/soak results | Release checklist passes on Windows 10 and Windows 11 with a published coverage matrix |
| Version 1.1 | Optional periodic AI review and explicit data-retention controls | Demonstrated usefulness without unacceptable false positives, privacy exposure, cost, or machine overhead |

P0 must test successful/refused/timed-out/firewall-blocked TCP, very short-lived processes, PID reuse, concurrent processes sharing an executable, connected and unconnected UDP/sendto to multiple peers, QUIC, IPv4/IPv6, ICMP, loopback/LAN/multicast/broadcast, services, and policy changes. Trace each test to expected observations and distinguish backend limitations from parser/correlation bugs.

P4 adds VPN/proxy and virtual-network scenarios, multiple users, Explorer restart, service crashes, reboot, sleep/resume, Event Log rollover/clearing, denied metadata access, disk exhaustion, database corruption, invalid imports, offline/stale feeds, large log searches, upgrades, and clean uninstall.

Measure duplicates, attribution confidence, and event delivery delay as well as missing events. Exercise Windows Firewall and common endpoint-security configurations without changing their intended decisions.

Any required coverage failure blocks an unconditional "monitor all outgoing connections" claim. Resolve it with a tested backend change or explicitly review the scope before release. Do not silently treat unsupported activity as trusted.

## 13. Decisions to record during implementation

- Verified machine architecture and precise Windows 11 builds included in release testing.
- Selected production collector and measured reasons for choosing it.
- Required privileges, audit/source settings, and ownership-aware uninstall behavior.
- Production threat feed, licensing, freshness thresholds, and update format.
- Final benchmark targets after the first measured baseline, with reasons for adjustments.
- Application license, signing identity, installer/update format, and any third-party distribution obligations.
- For version 1.1, chosen AI providers/models, evaluation results, retention mode, and resource/cost budgets.

The 0.4.0 preview implements the event-driven responsiveness follow-up: independent UI workers, immutable display/service snapshots, coalesced updates, bounded ordered actions, concurrent authenticated IPC and cancellation of obsolete history queries. Production message-loop diagnostics and stalled-work/refresh-burst regressions provide evidence for further tuning. Current architecture and measured results are maintained in docs/ARCHITECTURE.md and docs/VALIDATION.md; physical-input validation and the Windows 11/soak matrix remain open.

The 0.5.0 preview adds a native dark/light dashboard and offline destination map. It reuses the event-driven workers, provides country-level markers with distinct IP/process/port details, and explicitly distinguishes country estimates from host coordinates. Natural Earth map data is bundled with attribution and a reproducible, checksum-pinned simplification script. Accessibility/high-DPI/Windows 11 release validation remains open.

This document records the original design and remaining release roadmap. The later approved preview installation enables WFP connection auditing and runs the service/tray app; it installs no driver or firewall rules. AI/cloud review remains unimplemented.

