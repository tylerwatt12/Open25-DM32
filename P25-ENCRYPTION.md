# P25 Phase 1 encryption bench procedure

This procedure covers the opt-in Classic r8 encryption build for the DM-32UV.
Use the standard build prerequisites in [BUILD.md](BUILD.md).

```sh
python3 tools/build_radiofoundry.py --classic --p25-encryption --stack-check
python3 tools/prepare_web_firmware.py \
  --image build/radiofoundry-p25/build/dm32-p25-classic-encryption/rfoundry_dm32.bin \
  --out build/Open25-DM32-Classic-r8-Encryption.zip
```

The preset keeps Classic r8's stock frequency limits. Verify the saved test
channel is inside the Classic P25 TX window. For example, 449.975 MHz is
inside the existing UHF window; the previous prototype's 450.000 MHz test
frequency is at the excluded upper edge.

## Prepare and flash each radio

1. Confirm the radio is a DM-32UV and the application package identifies
   target `dm32-048`. Use a firmware package with **Tools → 0 P25 Keyloader**
   and the channel **Algorithm** selector.
2. Back up the radio before installing the encryption build. In the Firmware Utility,
   save the stock code and data flash backup for each radio, and export the
   codeplug. Keep each unit's backup separate. Follow the codec and first-install
   instructions in [BUILD.md](BUILD.md) so the radio's own compatible AMBE slot
   is preserved. A firmware ZIP contains the application and its manifest.
3. Flash one radio at a time using the existing DM-32UV Firmware Utility
   workflow. Check that it boots. Prepare an ordinary FM channel for keyloader
   operations and a saved conventional P25 channel for the voice test. Keep the
   serial utility and flasher closed when handing the port to another program.
4. Run the keyloader from the same source checkout as the firmware.
   Its upstream source directory supplies `rfoundry_debug.py`. Install Python
   3 and its serial dependency. Discover the port with
   `python -m serial.tools.list_ports`.

```powershell
python -m pip install pyserial
```

The port examples below use Windows `COM7`. Use the enumerated port for your
cable. The local programmer expects the radio in its normal application, on
an FM channel.

## Initialize the vault on the handset

1. Disconnect the programming cable. Select the FM channel.
2. Open **Menu → Tools → 0 P25 Keyloader**. On a fresh vault, enter an
   8–32 digit PIN, press **OK**, enter it again, and press **OK**. Use a long
   randomly chosen PIN and store it privately. Each radio may use its own PIN.
3. Wait for PIN checking to finish. **Unlocked. Load via cable** confirms
   vault creation and starts a two-minute loading window. Reconnect this radio
   to the programming cable. Keep the other handset disconnected while loading.
4. If the radio instead says **Enter vault PIN**, it already has a vault.
   Unlock it with its existing PIN. For a fresh cable loading window, follow
   the update procedure below.

## Load matching traffic keys

Use the same cipher, key ID, and traffic key on both ends of a test. Slot
numbers organize keys locally. Agree the cipher and KID before provisioning
and handle the traffic key through your authorized local process.

| Algorithm | CLI value | Key length |
| --- | --- | --- |
| AES-256 | `aes256` | 32 bytes, 64 hex digits |
| DES-OFB | `des-ofb` | 8 bytes, 16 hex digits |
| ADP/RC4 | `adp` | 5 bytes, 10 hex digits |

During the loading window, run the corresponding command. This example uses
slot 1 and an example KID of `0x1234`. The program prompts for the key with
terminal echo suppressed. Keep the key out of commands, chat, screenshots,
logs, and source files.

```powershell
python .\tools\p25_keyloader.py --port COM7 load 1 --algorithm aes256 --key-id 0x1234
python .\tools\p25_keyloader.py --port COM7 list
python .\tools\p25_keyloader.py --port COM7 finish
```

Check that `list` reports the expected slot, algorithm, and KID. The utility
lists metadata only. `finish` closes cable loading and leaves the vault
unlocked for radio use. Unplug the cable and repeat on the other radio.

### Update, remove, or lock a key

With the radio on FM and the cable connected, run `start`. Enter the existing
PIN on the handset, wait for **Unlocked. Load via cable**, and run `load`
again to replace a slot or `remove` to clear it. Finish after checking the result.

```powershell
python .\tools\p25_keyloader.py --port COM7 start
python .\tools\p25_keyloader.py --port COM7 remove 1
python .\tools\p25_keyloader.py --port COM7 list
python .\tools\p25_keyloader.py --port COM7 finish
```

Use `python .\tools\p25_keyloader.py --port COM7 lock` on FM to clear the
unlocked session from RAM immediately. Power cycling also locks the vault.
Finish any active transmission before changing or locking keys.

## Set the saved P25 channel

1. Edit or create a saved conventional **P25** channel. Enter the test RX/TX
   frequencies, NAC, talkgroup, and each handset's own P25 Radio ID. If the
   firmware offers separate RX and TX NAC fields, set both for the test.
   Both ends of a simplex bench test need matching frequencies and matching
   NAC/talkgroup. Use distinct local radio IDs.
2. Set **Encryption** to the loaded **Key slot 1** through **Key slot 8**.
   Set **Algorithm** to the stored cipher, or **Auto** to use the cipher stored
   in that slot. Save the channel and select it. **Off** selects clear operation.
3. Set **Quick → Encrypt** to **On**. The top-bar key icon shows encryption is
   selected. To confirm the key is available, check the slot on FM with
   `list` or perform the controlled voice test below.
4. Repeat on the other radio. Put both radios on the same saved P25 channel.

## Verify a cable-free reboot and voice

1. With the programming cable disconnected, reboot the first radio. Start on
   FM and open the Keyloader. Enter the existing PIN and wait for **Vault
   unlocked**. Normal radio operation can continue immediately. If checking
   slot metadata, reconnect the cable afterward and use `list` on FM.
   Disconnect before the RF test.
2. In a shielded or conducted bench setup, verify intelligible speech in both
   directions with matching AES-256 profiles. A two-radio prototype test
   passed on 450.000 MHz simplex, NAC `0x293`, talkgroup `1001`. Use a frequency
   within the limits of the firmware under test and suitable for your setup.
3. On the transmitter, select a known empty slot with **Quick → Encrypt**
   still on. Hold PTT briefly. Expect **NO TX / P25 key unavailable** and
   silence on the receiver. Restore the correct slot.
4. For the receive warning, keep the transmitter on AES-256 with its valid
   slot. Select **DES-OFB** on the receiver while it still points at its AES
   slot. Transmit only from the AES radio. The receiver should mute and
   display **Encrypt Mismatch**. Restore AES-256 on the receiver and verify
   speech returns. A received ALGID or KID mismatch is detectable. There is
   no authenticated voice check for different key bytes under the same IDs.

## Troubleshoot

| Symptom | Check |
| --- | --- |
| COM port missing or access denied | Confirm the cable enumerates, and disconnect the flasher, CPS, or serial terminal from the port. |
| Command is busy | Select FM, release PTT, and wait for any PIN check to finish. |
| Loading window closed | Run `start` and authorize again with the handset PIN. The vault can remain unlocked for radio use while the cable window is closed. |
| No flash ID during keypad setup | Run `start` to supply a per-vault salt from the PC, then create the PIN on the handset. |
| No erased vault block | Stop provisioning and retain the radio's backup. The keyloader requires an erased block in its reserved data-flash area. |
| P25 key unavailable | Unlock the vault and verify the selected slot and algorithm. |
| Encrypt Mismatch or no audio | Check transmit and receive frequency, NAC, talkgroup, the active cipher and KID, and that both ends used the same traffic key. |
| PIN rejected | Retry with the vault's original PIN. The workflow has no PIN recovery operation. |

## Provisioning and release notes

The keyloader derives the vault master from the PIN on the handset and stores
authenticated ciphertext in data flash. The PIN stays on the handset. The
traffic key crosses the direct serial cable during loading, so use a trusted
computer and cable. Flash possession permits offline PIN guesses. AES-256 is
the strongest of these options. DES-OFB and ADP/RC4 are legacy interoperability
modes. The radio reserves message-indicator state before transmit and can
unlock and use saved slots after reboot with the cable disconnected.

The earlier prototype was hardware-tested for matching AES audio in both directions,
first-time keypad setup, unplugged PIN unlock and slot persistence on one
radio, empty-slot PTT denial, and receive mismatch indication. DES-OFB and
ADP/RC4 passed host voice tests. They still need radio interoperability tests.
This procedure covers conventional group voice. The separate private
dialing and trunked call workflow remains outside this proposal.


## Build verification

Run the secure voice and vault tests with the pinned host references fetched
per [BUILD.md](BUILD.md).

```sh
python3 tools/test_voice.py --encryption
python3 tools/test_classic.py
```
