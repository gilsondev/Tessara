//! Album art → chunked `MEDIA_COVER` RGB565 lines for the panel.

use super::{MediaInfo, MediaState};

const CHUNK_RAW: usize = 480; // → 960 hex chars; fits console 2048 line

/// `MEDIA_COVER\tv1\t1\t<key>\t<w>\t<h>\trgb565le\t<n>\t<i>\t<hex>`
pub fn encode_cover_chunks(key: &str, w: u32, h: u32, rgb565_le: &[u8]) -> Vec<String> {
    let n = rgb565_le.len().div_ceil(CHUNK_RAW).max(1);
    let mut out = Vec::with_capacity(n);
    for (i, chunk) in rgb565_le.chunks(CHUNK_RAW).enumerate() {
        let hex: String = chunk.iter().map(|b| format!("{b:02x}")).collect();
        out.push(format!(
            "MEDIA_COVER\tv1\t1\t{key}\t{w}\t{h}\trgb565le\t{n}\t{i}\t{hex}"
        ));
    }
    out
}

pub fn cover_key(info: &MediaInfo) -> String {
    let raw = format!("{}|{}|{}", info.app, info.title, info.artist);
    let mut h: u64 = 0xcbf29ce484222325;
    for b in raw.bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    format!("{h:016x}")
}

pub fn rgba_to_rgb565_le(rgba: &[u8], w: u32, h: u32) -> Vec<u8> {
    let mut out = Vec::with_capacity((w * h * 2) as usize);
    for px in rgba.chunks_exact(4) {
        let r5 = (px[0] as u16) >> 3;
        let g6 = (px[1] as u16) >> 2;
        let b5 = (px[2] as u16) >> 3;
        let v = (r5 << 11) | (g6 << 5) | b5;
        out.push((v & 0xff) as u8);
        out.push((v >> 8) as u8);
    }
    out
}

pub fn jpeg_or_png_to_rgb565(bytes: &[u8], edge: u32) -> Option<Vec<u8>> {
    let img = image::load_from_memory(bytes).ok()?;
    let resized = img.resize_exact(edge, edge, image::imageops::FilterType::Triangle);
    let rgba = resized.to_rgba8();
    Some(rgba_to_rgb565_le(rgba.as_raw(), edge, edge))
}

#[cfg(target_os = "macos")]
pub fn fetch_cover_bytes(info: &MediaInfo) -> Option<Vec<u8>> {
    if info.state == MediaState::Stopped || info.title.is_empty() {
        return None;
    }
    let app = info.app.to_ascii_lowercase();
    if app.contains("spotify") {
        fetch_spotify_artwork()
    } else if app.contains("music") {
        fetch_music_artwork()
    } else {
        None
    }
}

#[cfg(not(target_os = "macos"))]
pub fn fetch_cover_bytes(_info: &MediaInfo) -> Option<Vec<u8>> {
    None
}

#[cfg(target_os = "macos")]
fn fetch_spotify_artwork() -> Option<Vec<u8>> {
    let script = r#"
    try
      if application "Spotify" is running then
        tell application "Spotify"
          if player state is not stopped then
            return artwork url of current track
          end if
        end tell
      end if
    end try
    return ""
    "#;
    let out = std::process::Command::new("osascript")
        .args(["-e", script])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let url = String::from_utf8_lossy(&out.stdout).trim().to_string();
    if url.is_empty() || !url.starts_with("http") {
        return None;
    }
    use std::io::Read;
    let resp = ureq::get(&url).call().ok()?;
    let mut buf = Vec::new();
    resp.into_reader()
        .take(2_000_000)
        .read_to_end(&mut buf)
        .ok()?;
    if buf.is_empty() {
        None
    } else {
        Some(buf)
    }
}

#[cfg(target_os = "macos")]
fn fetch_music_artwork() -> Option<Vec<u8>> {
    let path = std::env::temp_dir().join("tessera-cover.img");
    let path_s = path.to_string_lossy().replace('\\', "\\\\");
    let script = format!(
        r#"
    try
      if application "Music" is running then
        tell application "Music"
          if player state is not stopped then
            set a to artwork 1 of current track
            set raw to raw data of a
            set f to open for access POSIX file "{path_s}" with write permission
            set eof f to 0
            write raw to f
            close access f
            return "ok"
          end if
        end tell
      end if
    end try
    return ""
    "#
    );
    let out = std::process::Command::new("osascript")
        .args(["-e", &script])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let ok = String::from_utf8_lossy(&out.stdout);
    if !ok.contains("ok") {
        return None;
    }
    std::fs::read(&path).ok().filter(|b| !b.is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rgb565_one_pixel() {
        let rgba = [255u8, 0, 0, 255];
        let out = rgba_to_rgb565_le(&rgba, 1, 1);
        assert_eq!(out.len(), 2);
        // R=31 → 0xF800 LE = 00 F8
        assert_eq!(out[0], 0x00);
        assert_eq!(out[1], 0xf8);
    }

    #[test]
    fn chunks_cover_full_buffer() {
        let raw = vec![0u8; CHUNK_RAW * 2 + 10];
        let lines = encode_cover_chunks("abc", 200, 200, &raw);
        assert_eq!(lines.len(), 3);
        assert!(lines[0].starts_with("MEDIA_COVER\tv1\t1\tabc\t200\t200\trgb565le\t3\t0\t"));
        assert!(lines[2].contains("\t2\t"));
    }
}
