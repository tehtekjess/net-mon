# Development

Build x64 Release with scripts/build.ps1. The VS2019 preset matches the installed development toolchain; vs2022 uses that Visual Studio generator and an available Windows SDK.

The source separates collection, Windows process inspection, deterministic rules, bounded history, authenticated IPC, service lifetime, and native UI. Keep expensive enrichment outside the collector and UI threads. Keep rule matching independent of transient PID/source ports.

Use the checked-in clang-format configuration. Windows header ordering is intentional. Project warnings are errors; do not suppress new warnings globally.

Run the six CTest targets after relevant changes to matching, persistence, identity, the virtual table, worker coordination or diagnostics. Run scripts/test-demo.ps1 and scripts/test-events.ps1 after IPC changes. UI concurrency changes also need scripts/test-ui-responsiveness.ps1 -Stress, with delayed/superseded requests and rapid refreshes, and a separate -Live run. These use the actual production window code but internal control actions; they do not measure physical pointer delivery. Compare installed process CPU and message diagnostics as well. Attaching the desktop accessibility inspector has itself caused a UI Automation CPU/event backlog in the test environment; detach it and allow the backlog to drain before measuring normal overhead. Collector changes also need the installed live checks on an authorized Windows machine: synthetic data cannot validate Windows event behavior.

Service changes require an elevated installation/update. The installer enables machine-wide Filtering Platform Connection auditing, preserves prior policy for uninstall, and leaves firewall rules unchanged. Use Demo.cmd for development that does not require live traffic.

Run build/Release/netmon_ui_preview.exe after visual/control changes. It renders production controls with synthetic records into build/ui-preview and checks map/dropdown/theme behavior. Review both palettes and compact layouts. The map model suite covers geographic grouping and exclusions. Keep map source provenance and checksums in data/README.md current when updating geometry.

Keep captured personal connection data out of source, bug reports, and shared archives. Fixtures use synthetic process paths and reserved/local addresses, plus fixed public IPs for offline geolocation/map checks; those fixtures do not contact the destinations. Reports should include the build, Windows version, collector health, and a minimal reproduction with sensitive values removed.

Do not add private driver IOCTL dependencies or a cloud AI component to the core collector. Optional AI review is a separate v1.1 roadmap item with explicit privacy and retention choices.

Before publishing validation reports, remove personal identities, exact run timestamps, machine-specific settings and captured traffic. Keep raw reports in the ignored test-results directory and captures/exports in their ignored directories. Review a sanitized copy before committing it. Use a repository-local GitHub noreply identity for public commits; GitHub email privacy also needs to be enabled for commits created through its website.
