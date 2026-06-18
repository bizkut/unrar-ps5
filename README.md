# unrar-ps5

PS5 ELF payload for extracting RAR and 7z archives on a jailbroken PS5 and installing extracted apps into a configurable `<extract_location>/<TitleID>-app` layout.

It is meant for archives that contain a PS5 app folder with:

```text
sce_sys/param.json
```

The payload reads the TitleID from `param.json` and installs to:

```text
<extract_location>/<TitleID>-app/
```

Default example:

```text
/data/homebrew/PPSA06465-app/sce_sys/param.json
```

## Quick Start

1. Copy `unrar_ps5.elf` to your computer.
2. Put your `.rar`, `.7z`, or `.7z.001` archive somewhere the PS5 can read it.
   - Default archive location: `/data/unrar`
   - USB archive locations are supported through `archive_location=`
3. Optional: create a `config.ini`.
4. Inject the payload:

```sh
nc -w 10 ps5ip 9021 < unrar_ps5.elf
```

Or use the Makefile:

```sh
make send PS5_HOST=ps5ip PS5_PORT=9021
```

If no config exists, the payload creates:

```text
/data/unrar/config.ini
```

## Config Location

The payload looks for a valid `config.ini` on USB first, then falls back to internal storage.

Search priority:

```text
<USB>/unrar/config.ini
<USB>/config.ini
/data/unrar/config.ini
```

`<USB>/unrar/config.ini` is preferred because it avoids accidentally using an unrelated root-level config file. A USB config is accepted only if it contains at least one known unrar-ps5 key, such as `filename=`, `archive_location=`, `extract_location=`, or `archive_password=`.

Logs and the lock file stay under:

```text
/data/unrar/
```

Temporary extraction staging is created under the effective install location:

```text
<extract_location>/.unrar-staging/
```

Because sidecar `.ini` files are applied before staging is created, a sidecar such as `extract_location=/mnt/usb1/homebrew` stages on the USB drive instead of extracting to internal storage first.

## Per-Archive Config

You can put an `.ini` file next to a RAR or 7z archive to override the main config for that archive only. The preferred sidecar `.ini` uses the same base name as the archive set:

```text
/mnt/usb1/unrar/PPSA17221.rar
/mnt/usb1/unrar/PPSA17221.ini
```

For multipart archives, use the archive set name:

```text
/mnt/usb1/unrar/PPSA17221.part1.rar
/mnt/usb1/unrar/PPSA17221.ini
/mnt/usb1/unrar/PPSA17221.7z.001
/mnt/usb1/unrar/PPSA17221.ini
```

If the archive path contains a TitleID, a matching TitleID `.ini` in the same folder is also valid:

```text
/mnt/usb1/unrar/My_Game_PPSA17221.part1.rar
/mnt/usb1/unrar/PPSA17221.ini
```

When the archive path contains a TitleID, the payload can also use `PPSA17221.ini` from the normal config locations:

```text
<USB>/unrar/PPSA17221.ini
<USB>/PPSA17221.ini
/data/unrar/PPSA17221.ini
```

Archive-local `.ini` files are checked first. The normal config locations are a fallback for TitleID `.ini` files only when the archive path contains that TitleID.

The payload reads `config.ini` first, builds the archive queue, then applies the sidecar `.ini` before extracting that archive. This lets sidecar files set archive-specific values such as `archive_password=`, `archive_location=`, `extract_location=`, `delete_after=`, `threads=`, `nice=`, `cpu_mask=`, or `progress=`.

`rar_password=` and `rar_location=` remain supported aliases for older configs. `filename=` entries in a sidecar `.ini` are ignored.

## Config Example

Default config:

```ini
filename=
archive_location=/data/unrar
archive_password=
delete_after=0
extract_location=/data/homebrew
threads=0
nice=-20
cpu_mask=0
progress=10
```

USB archive example:

```ini
filename=
archive_location=/mnt/usb0/unrar
archive_password=
delete_after=0
extract_location=/data/homebrew
threads=0
nice=-20
cpu_mask=0
progress=10
```

Password example:

```ini
filename=
archive_location=/data/unrar
archive_password=your_password
delete_after=0
extract_location=/data/homebrew
threads=0
nice=-20
cpu_mask=0
progress=10
```

## Config Options

| Key | Default | Description |
| --- | --- | --- |
| `filename` | empty | Archive to extract. Can be repeated in the main `config.ini`. If no `filename=` is set, the payload scans `.rar`, `.7z`, and `.7z.001` archive starts recursively under `archive_location`, skips already installed TitleIDs, and stops after the first archive that installs. Relative paths resolve under `archive_location`; absolute paths are used as-is. Ignored in sidecar `.ini` files. |
| `archive_location` | `/data/unrar` | Base directory for archive search and relative `filename=` values. Set this to a USB path to extract archives from external storage. |
| `archive_password` | empty | RAR or 7z password. Empty means no password is used. |
| `rar_location` | `/data/unrar` | Backward-compatible alias for `archive_location`. |
| `rar_password` | empty | Backward-compatible alias for `archive_password`. |
| `delete_after` | `0` | Set to `1`, `true`, `yes`, or `on` to delete archive volumes after a successful install. RAR deletes detected multipart volumes; single-file 7z deletes the `.7z`; multipart 7z deletes sibling `name.7z.001`, `.002`, etc. |
| `extract_location` | `/data/homebrew` | Base install directory. Final path becomes `<extract_location>/<TitleID>-app`. |
| `threads` | `0` | Archive engine thread count. `0` lets UnRAR or 7-Zip choose its default. Explicit 7z values are clamped to `1`-`8`. |
| `nice` | `-20` | Process priority from `-20` to `20`. Invalid values are ignored. |
| `cpu_mask` | `0` | Optional CPU affinity mask, for example `0xff`. `0` disables affinity pinning. Some kernels may reject affinity changes; check the log. |
| `progress` | `10` | Notification interval in percent. For example, `10` notifies every 10%; `5` notifies every 5%. Values are clamped to `1`-`100`. |

## Multiple Archives

You can repeat `filename=` to extract more than one archive sequentially:

```ini
filename=game.rar
filename=game2.7z
filename=game3.7z.001
```

Multipart duplicates are collapsed automatically. For example:

```ini
filename=game.rar
filename=game.r00
filename=game2.7z.001
filename=game2.7z.002
```

Only `game.rar` and `game2.7z.001` are processed. RAR continuation volumes and `.7z.002+` files are treated as dependent volumes, not separate archives.

The same applies to part-style RAR archives:

```ini
filename=game.part1.rar
filename=game.part2.rar
```

Only `game.part1.rar` is processed.

Archives are extracted one at a time. Parallel extraction is intentionally not used because it would compete for CPU, disk I/O, memory, and install-path state on the PS5.

When `filename=` is empty, auto-discovery scans all `.rar`, `.7z`, and `.7z.001` archive starts under `archive_location`. If an archive path contains a TitleID and `<extract_location>/<TitleID>-app` already exists, that archive is skipped before extraction and the payload continues to the next archive. Auto-discovery stops after the first archive that successfully installs.

## Notifications And Logs

The payload sends PS5 notifications for:

- Extraction start.
- Progress at the configured `progress=` interval, defaulting to every 10%.
- Successful install with TitleID and final path.
- Errors with a short reason.

Detailed logs are appended to:

```text
/data/unrar/unrar.log
```

Useful log fields include selected config path, selected archive, `archive_type=rar|7z`, archive location, extract location, `stage_path=`, delete-after state, thread count, progress interval, priority result, CPU mask result, extraction time, normalization time, TitleID, and final path. Multipart 7z failures include `missing_volume=` when the missing part is known.

## Troubleshooting

- `no .rar or .7z file found`: upload an archive under `archive_location`, set `archive_location=`, or set `filename=`.
- `bad password` or `checksum or password error`: set `archive_password=` correctly.
- `missing 7z volume or open error`: make sure all `name.7z.001`, `.002`, etc. files are present next to each other.
- `unsupported 7z method`: the archive uses a 7z method outside the bundled extract-only method set.
- `sce_sys/param.json not found after extraction`: the archive does not contain the expected PS5 app folder structure.
- `TitleID not found`: `param.json` exists but does not contain a supported TitleID key.
- `already installed`: `<extract_location>/<TitleID>-app` already exists, so that archive is skipped and the existing folder is left untouched.
- `extraction already running`: another payload instance is active.
- `cpu_mask ... result=fail`: CPU affinity was rejected; leave `cpu_mask=0`.

## Technical Details

On injection, the payload:

1. Creates `/data/unrar/config.ini` if no valid USB config or internal config exists.
2. Reads config from USB first when a valid USB config is present.
3. Builds an archive queue from repeated `filename=` entries, or auto-discovers archives recursively under `archive_location`.
4. Detects archive type by magic bytes first, then extension fallback.
5. Collapses duplicate multipart entries so each archive set extracts once.
6. Applies optional scheduling settings.
7. Extracts each archive into `<extract_location>/.unrar-staging`.
8. Finds `sce_sys/param.json`, reads the TitleID, and moves the extracted app into `<extract_location>/<TitleID>-app` when that folder does not already exist.
9. Optionally deletes archive volumes after a successful extraction.
10. Writes progress and errors to notifications and `/data/unrar/unrar.log`.

A lock file at `/data/unrar/unrar.lock` prevents overlapping injections. The lock uses an OS file lock, so stale lock files from a crash should not block later runs.

## Performance Notes

Development testing found the fastest stable defaults were:

```ini
threads=0
nice=-20
cpu_mask=0
progress=10
```

`threads=0` lets the archive engine choose its internal threading. Explicit values from `1` to `8` are accepted for both engines; 7z values are passed to the 7-Zip handler as `mt`.

## Build

Build with the Docker SDK image:

```sh
docker run --rm \
  -v "$PWD:/work" \
  -w /work \
  -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk \
  ps5-payload-sdk:libcxx \
  make clean all
```

The output ELF is:

```text
unrar_ps5.elf
```

There is also a convenience target when the host `PS5_PAYLOAD_SDK` path exists:

```sh
make docker-build
```
