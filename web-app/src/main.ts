import './styles.css'
import { mountApp } from './app'
import { ChargerClient } from './client'
import { isBluetoothAvailable, WebBluetoothTransport } from './transport'

const client = new ChargerClient(new WebBluetoothTransport())
mountApp(client)

if (!isBluetoothAvailable()) {
  const connect = document.getElementById('connect') as HTMLButtonElement | null
  const hint = document.getElementById('pairing-hint')
  if (connect) connect.disabled = true
  if (hint) {
    hint.textContent =
      'This browser has no Web Bluetooth support. On iOS, open this page in Bluefy; on desktop, use Chrome or Edge.'
  }
}
