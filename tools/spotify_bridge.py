#!/usr/bin/env python3
"""Show the currently playing Spotify track and synced lyrics on a NodeMCU TFT."""

from __future__ import annotations

import argparse
import bisect
import json
import re
import subprocess
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass

import serial
from serial.tools import list_ports


MPRIS_DESTINATION = "org.mpris.MediaPlayer2.spotify"
MPRIS_OBJECT = "/org/mpris/MediaPlayer2"
MPRIS_INTERFACE = "org.mpris.MediaPlayer2.Player"
LRCLIB_BASE_URL = "https://lrclib.net/api"
LRCLIB_USER_AGENT = "NodeMCU-Lyrics/1.0 (local personal display)"
MAX_DISPLAY_FIELD = 150


@dataclass(frozen=True)
class PlaybackState:
    connected: bool = False
    playing: bool = False
    position_ms: int = 0
    duration_ms: int = 0
    track_id: str = ""
    title: str = ""
    artist: str = ""
    album: str = ""


@dataclass(frozen=True)
class LyricsResult:
    status: str
    lines: tuple[tuple[int, str], ...] = ()


def display_text(value: str, limit: int = MAX_DISPLAY_FIELD) -> str:
    """Make a safe, single-field string supported by the TFT built-in font."""
    value = value.replace("|", " ").replace("\r", " ").replace("\n", " ").strip()
    value = unicodedata.normalize("NFKD", value).encode("ascii", "replace").decode()
    value = re.sub(r"\s+", " ", value)
    return value[:limit] or " "


def find_serial_port(explicit_port: str | None) -> str:
    if explicit_port:
        return explicit_port

    ports = list(list_ports.comports())
    cp2102 = [p.device for p in ports if p.vid == 0x10C4 and p.pid == 0xEA60]
    if len(cp2102) == 1:
        return cp2102[0]
    if len(ports) == 1:
        return ports[0].device

    devices = ", ".join(p.device for p in ports) or "tidak ada"
    raise RuntimeError(
        f"Port NodeMCU tidak dapat dipilih otomatis (port tersedia: {devices}). "
        "Gunakan --port /dev/ttyUSB0."
    )


def match(pattern: str, text: str, default: str = "") -> str:
    result = re.search(pattern, text)
    return result.group(1) if result else default


def gvariant_text(raw: str) -> str:
    return raw.replace("\\'", "'").replace('\\"', '"').replace("\\\\", "\\")


def variant_string(key: str, raw: str, array: bool = False) -> str:
    """Extract a GVariant string printed with either single or double quotes."""
    array_start = r"\[" if array else ""
    pattern = (
        rf"'{re.escape(key)}': <{array_start}"
        rf"(?:'((?:\\.|[^'])*)'|\"((?:\\.|[^\"])*)\")"
    )
    result = re.search(pattern, raw)
    if not result:
        return ""
    return gvariant_text(result.group(1) if result.group(1) is not None else result.group(2))


def read_spotify_state() -> PlaybackState | None:
    """Return None for a transient D-Bus failure, not a disconnected state."""
    command = [
        "gdbus", "call", "--session", "--dest", MPRIS_DESTINATION,
        "--object-path", MPRIS_OBJECT, "--method",
        "org.freedesktop.DBus.Properties.GetAll", MPRIS_INTERFACE,
    ]
    try:
        result = subprocess.run(
            command, check=True, capture_output=True, text=True, timeout=2
        )
    except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None

    raw = result.stdout
    status = match(r"'PlaybackStatus': <'([^']+)'", raw)
    position_us = int(match(r"'Position': <int64 (\d+)>", raw, "0"))
    duration_us = int(match(r"'mpris:length': <u?int64 (\d+)>", raw, "0"))
    track_path = variant_string("mpris:trackid", raw)
    title = variant_string("xesam:title", raw)
    artist = variant_string("xesam:artist", raw, array=True)
    album = variant_string("xesam:album", raw)
    track_id = track_path.rsplit("/", 1)[-1] if track_path else ""

    return PlaybackState(
        connected=bool(title),
        playing=status == "Playing",
        position_ms=position_us // 1000,
        duration_ms=duration_us // 1000,
        track_id=display_text(track_id, 48).strip(),
        title=display_text(title, 100).strip(),
        artist=display_text(artist, 100).strip(),
        album=album,
    )


def parse_lrc(source: str) -> tuple[tuple[int, str], ...]:
    timestamp = re.compile(r"\[(\d+):(\d{1,2})(?:[.:](\d{1,3}))?\]")
    offset_match = re.search(r"\[offset:([+-]?\d+)\]", source, re.IGNORECASE)
    offset_ms = int(offset_match.group(1)) if offset_match else 0
    parsed: list[tuple[int, str]] = []

    for raw_line in source.splitlines():
        stamps = list(timestamp.finditer(raw_line))
        if not stamps:
            continue
        lyric = timestamp.sub("", raw_line).strip()
        if not lyric:
            continue
        for stamp in stamps:
            fraction = stamp.group(3) or "0"
            fraction_ms = int(fraction.ljust(3, "0")[:3])
            milliseconds = (
                int(stamp.group(1)) * 60_000
                + int(stamp.group(2)) * 1_000
                + fraction_ms
                + offset_ms
            )
            parsed.append((max(milliseconds, 0), display_text(lyric)))

    parsed.sort(key=lambda item: item[0])
    return tuple(parsed)


def lrclib_request(endpoint: str, parameters: dict[str, str | int]) -> object:
    url = f"{LRCLIB_BASE_URL}/{endpoint}?{urllib.parse.urlencode(parameters)}"
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": LRCLIB_USER_AGENT,
            "Lrclib-Client": LRCLIB_USER_AGENT,
            "Accept": "application/json",
        },
    )
    with urllib.request.urlopen(request, timeout=6) as response:
        return json.load(response)


def normalized(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.casefold())


def fetch_lyrics(state: PlaybackState) -> LyricsResult:
    parameters: dict[str, str | int] = {
        "track_name": state.title,
        "artist_name": state.artist,
        "duration": round(state.duration_ms / 1000),
    }
    if state.album:
        parameters["album_name"] = state.album

    result: dict[str, object] | None = None
    try:
        response = lrclib_request("get", parameters)
        if isinstance(response, dict):
            result = response
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, json.JSONDecodeError):
        pass

    if not result or not result.get("syncedLyrics"):
        try:
            response = lrclib_request(
                "search", {"track_name": state.title, "artist_name": state.artist}
            )
            candidates = [
                item for item in response
                if isinstance(item, dict) and item.get("syncedLyrics")
            ] if isinstance(response, list) else []

            def score(item: dict[str, object]) -> float:
                item_title = normalized(str(item.get("trackName", "")))
                item_artist = normalized(str(item.get("artistName", "")))
                duration = float(item.get("duration", 0) or 0) # type: ignore
                value = 100 if item_title == normalized(state.title) else 0
                value += 60 if item_artist == normalized(state.artist) else 0
                value -= min(abs(duration - state.duration_ms / 1000), 60)
                return value

            if candidates:
                result = max(candidates, key=score)
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            return LyricsResult("error")

    if not result:
        return LyricsResult("missing")
    if bool(result.get("instrumental")):
        return LyricsResult("instrumental")

    synced = result.get("syncedLyrics")
    lines = parse_lrc(str(synced)) if synced else ()
    return LyricsResult("synced", lines) if lines else LyricsResult("missing")


def lyric_window(result: LyricsResult, position_ms: int) -> tuple[str, str, str]:
    if not result.lines:
        return " ", " ", " "
    times = [line[0] for line in result.lines]
    index = bisect.bisect_right(times, position_ms) - 1
    if index < 0:
        return " ", "...", result.lines[0][1]
    previous = result.lines[index - 1][1] if index > 0 else " "
    current = result.lines[index][1]
    following = result.lines[index + 1][1] if index + 1 < len(result.lines) else " "
    return previous, current, following


def encode_state(state: PlaybackState) -> bytes:
    if not state.connected:
        return b"SPOT|0\n"
    fields = [
        "SPOT", "1", "1" if state.playing else "0",
        str(state.position_ms), str(state.duration_ms),
        display_text(state.track_id, 48), display_text(state.title, 100),
        display_text(state.artist, 100),
    ]
    return ("|".join(fields) + "\n").encode("ascii", "replace")


def encode_lyrics(result: LyricsResult, position_ms: int) -> bytes:
    previous, current, following = lyric_window(result, position_ms)
    fields = [
        "LYRIC", result.status, display_text(previous),
        display_text(current), display_text(following),
    ]
    return ("|".join(fields) + "\n").encode("ascii", "replace")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Kirim Spotify Desktop dan lirik sinkron ke TFT NodeMCU."
    )
    parser.add_argument("--port", help="Port serial, contoh: /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=0.25)
    args = parser.parse_args()

    try:
        port = find_serial_port(args.port)
        connection = serial.Serial(port=None, baudrate=args.baud, timeout=0.1)
        connection.dtr = False
        connection.rts = False
        connection.port = port
        connection.open()
    except (RuntimeError, serial.SerialException) as error:
        print(f"Gagal membuka NodeMCU: {error}", file=sys.stderr)
        return 1

    print(f"NodeMCU terhubung di {port}. Menunggu restart board...")
    time.sleep(2.5)
    connection.reset_input_buffer()

    last_summary: tuple[bool, bool, str] | None = None
    last_good_state: PlaybackState | None = None
    failures = 0
    lyric_track_id = ""
    lyrics = LyricsResult("loading")
    lyric_future: Future[LyricsResult] | None = None
    last_lyric_message: tuple[str, str, str, str] | None = None
    executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="lrclib")

    try:
        while True:
            fresh_state = read_spotify_state()
            if fresh_state is None:
                failures += 1
                state = last_good_state if failures < 8 and last_good_state else PlaybackState()
            else:
                failures = 0
                state = fresh_state
                last_good_state = state

            if state.connected and state.track_id != lyric_track_id:
                lyric_track_id = state.track_id
                lyrics = LyricsResult("loading")
                lyric_future = executor.submit(fetch_lyrics, state)
                last_lyric_message = None
                print(f"Mencari lirik: {state.artist} - {state.title}")

            if lyric_future is not None and lyric_future.done():
                try:
                    lyrics = lyric_future.result()
                except Exception as error:  # Keep the serial bridge alive on provider errors.
                    print(f"Pencarian lirik gagal: {error}", file=sys.stderr)
                    lyrics = LyricsResult("error")
                lyric_future = None
                print(f"Status lirik: {lyrics.status} ({len(lyrics.lines)} baris)")

            connection.write(encode_state(state))
            if state.connected:
                lyric_message = (lyrics.status, *lyric_window(lyrics, state.position_ms))
                if lyric_message != last_lyric_message:
                    connection.write(encode_lyrics(lyrics, state.position_ms))
                    last_lyric_message = lyric_message

            summary = (state.connected, state.playing, state.track_id)
            if summary != last_summary:
                if state.connected:
                    status = "playing" if state.playing else "paused"
                    print(f"Spotify: {state.artist} - {state.title} ({status})")
                else:
                    print("Spotify Desktop belum terdeteksi.")
                last_summary = summary
            time.sleep(max(args.interval, 0.1))
    except KeyboardInterrupt:
        print("\nBridge dihentikan.")
    except serial.SerialException as error:
        print(f"Koneksi serial terputus: {error}", file=sys.stderr)
        return 1
    finally:
        executor.shutdown(wait=False, cancel_futures=True)
        connection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
