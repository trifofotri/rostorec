# rostorec

*Infinite cloud storage with roblox places!!!*

`put` splits a file into parts, wraps each part in a tiny place file (a single `StringValue`), and uploads each one as a new version of a place through [Open Cloud API](https://create.roblox.com/docs/cloud/guides/usage-place-publishing).

## Build

```
git clone --recurse-submodules https://github.com/trifofotri/rostorec.git
cd restorec
cmake -B build && cmake --build build
```

## API key

You need to make an Open Cloud API key for your account with these permissions and add your experience to both:

- Place publishing (`universe-places:write`)
- Legacy asset access (`legacy-asset:manage`)

## Usage

Upload a file:

```
./rostorec $(cat key) <universe-id> <place-id> put <file> <manifest.json>
```

Download it again:

```
./rostorec $(cat key) <universe-id> <place-id> get <manifest.json> <file-name> <out-path>
```

Example:

```
./rostorec $(cat key) 10401067237 116736198104337 put video.mp4 manifest.json
./rostorec $(cat key) 10401067237 116736198104337 get manifest.json video.mp4 restored.mp4
cmp video.mp4 restored.mp4
```

`<file-name>` is the file's base name, so `put /home/me/video.mp4` is fetched as `video.mp4`. The manifest can hold many files, and uploading the same name again replaces its entry.

## manifest.json

```json
{
  "files": {
    "video.mp4": {
      "total_size": 331000000,
      "universe_id": 10401067237,
      "place_id": 116736198104337,
      "parts": [
        { "version": 6, "size": 104857600 },
        { "version": 7, "size": 104857600 },
        { "version": 8, "size": 121284800 }
      ]
    }
  }
}
```

Keep this file safe.

## Warnings

- **Every upload replaces the entire place.** Use some place that you don't care about.
- **Parts are 100 MB each** (`PART_SIZE` in `main.c`). I'm too lazy to figure out roblox's upload limit or whatever.
- **Don't use this as your only copy of anything important.**