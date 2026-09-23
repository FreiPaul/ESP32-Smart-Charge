# Smart Charger Web App

The charger UI as a web page. Same BLE protocol as the Flutter app in
`mobile-app/`, no install on the phone.

## Running it on an iPhone

iOS Safari has no Web Bluetooth. Open the page in [Bluefy][bluefy] instead,
which ships its own Web Bluetooth implementation.

Web Bluetooth only works in a secure context, so the page has to come from
`https://` or `http://localhost`. `npm run build` emits a single
self-contained `dist/index.html` with no external assets, which can be served
from any static host.

[bluefy]: https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055

## Differences from the Flutter app

Web Bluetooth has no scan API. The browser shows its own device chooser
instead, so there is no in-app device list with signal strength.

On reconnect, the start time is only restored when the charger still has its
schedule enabled. The Flutter app restores the last start time even after a
finished charge, where the firmware has already cleared the schedule flag, so
it shows a time that is no longer armed.

Everything else behaves the same.

## Development

```bash
npm ci
npm run dev      # dev server on localhost, Web Bluetooth works there
npm run lint
npm test         # unit tests for the wire protocol and the client
npm run e2e      # Playwright, drives the UI against a simulated charger
npm run build
```

## Layout

| Path                    | Contains                                                  |
| ----------------------- | --------------------------------------------------------- |
| `src/protocol.ts`       | Wire format: settings, status, commands, UUIDs            |
| `src/transport.ts`      | `ChargerTransport` interface and its Web Bluetooth driver |
| `src/client.ts`         | Protocol semantics on top of a transport                  |
| `src/app.ts`            | UI wiring                                                 |
| `e2e/fake-charger.ts`   | Web Bluetooth stand-in that mirrors the firmware FSM      |

The E2E suite installs `e2e/fake-charger.ts` in place of `navigator.bluetooth`
and runs the whole app against it, including the real transport, so schedules,
threshold cutoff and manual control are verified without an ESP32 on the bench.
