//! macOS host wakes for MEDIA / FOCUS / THEME — distributed notifications + CFRunLoop.
//!
//! Non-macOS builds expose a stub that never sends wakes (poll-only).

use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HostWake {
    Media,
    Focus,
    Theme,
}

static MEDIA_TX: OnceLock<Sender<HostWake>> = OnceLock::new();
static FOCUS_TX: OnceLock<Sender<HostWake>> = OnceLock::new();
static THEME_TX: OnceLock<Sender<HostWake>> = OnceLock::new();

/// Start the observer thread (idempotent). Returns the receiver end for the USB loop.
pub fn start() -> Receiver<HostWake> {
    let (tx, rx) = mpsc::channel();
    let _ = MEDIA_TX.set(tx.clone());
    let _ = FOCUS_TX.set(tx.clone());
    let _ = THEME_TX.set(tx);
    #[cfg(target_os = "macos")]
    macos::spawn_run_loop();
    rx
}

/// Drain coalesced wakes into flags (media / focus / theme).
pub fn drain(rx: &Receiver<HostWake>) -> (bool, bool, bool) {
    let mut media = false;
    let mut focus = false;
    let mut theme = false;
    while let Ok(w) = rx.try_recv() {
        match w {
            HostWake::Media => media = true,
            HostWake::Focus => focus = true,
            HostWake::Theme => theme = true,
        }
    }
    (media, focus, theme)
}

/// Current macOS appearance (`defaults read -g AppleInterfaceStyle` → Dark).
pub fn macos_prefers_dark() -> bool {
    #[cfg(target_os = "macos")]
    {
        macos::prefers_dark()
    }
    #[cfg(not(target_os = "macos"))]
    {
        false
    }
}

pub fn theme_line(dark: bool) -> String {
    format!(
        "THEME\tv1\t1\t{}",
        if dark { "dark" } else { "light" }
    )
}

#[cfg(target_os = "macos")]
mod macos {
    use super::{HostWake, FOCUS_TX, MEDIA_TX, THEME_TX};
    use std::ffi::c_void;
    use std::os::raw::c_char;
    use std::process::Command;
    use std::ptr;
    use std::thread;

    type CFNotificationCenterRef = *mut c_void;
    type CFStringRef = *const c_void;
    type CFDictionaryRef = *const c_void;
    type CFRunLoopRef = *mut c_void;

    type CFNotificationCallback = Option<
        unsafe extern "C" fn(
            CFNotificationCenterRef,
            *mut c_void,
            CFStringRef,
            *const c_void,
            CFDictionaryRef,
        ),
    >;

    #[repr(C)]
    #[allow(dead_code)]
    enum CFNotificationSuspensionBehavior {
        Drop = 1,
        Coalesce = 2,
        Hold = 3,
        DeliverImmediately = 4,
    }

    #[link(name = "CoreFoundation", kind = "framework")]
    extern "C" {
        fn CFNotificationCenterGetDistributedCenter() -> CFNotificationCenterRef;
        fn CFNotificationCenterAddObserver(
            center: CFNotificationCenterRef,
            observer: *const c_void,
            callBack: CFNotificationCallback,
            name: CFStringRef,
            object: *const c_void,
            suspensionBehavior: CFNotificationSuspensionBehavior,
        );
        fn CFStringCreateWithCString(
            alloc: *const c_void,
            cStr: *const c_char,
            encoding: u32,
        ) -> CFStringRef;
        fn CFRunLoopGetCurrent() -> CFRunLoopRef;
        fn CFRunLoopRun();
    }

    const K_CF_STRING_ENCODING_UTF8: u32 = 0x0800_0100;

    const MEDIA_NAMES: &[&str] = &[
        "com.apple.Music.playerInfo",
        "com.apple.iTunes.playerInfo",
        "com.spotify.client.PlaybackStateChanged",
    ];

    const FOCUS_NAMES: &[&str] = &[
        "_NSDoNotDisturbEnabledNotification",
        "_NSDoNotDisturbDisabledNotification",
    ];

    // Posted when the user flips System Settings → Appearance.
    const THEME_NAMES: &[&str] = &[
        "AppleInterfaceThemeChangedNotification",
        "AppleColorPreferencesChangedNotification",
    ];

    unsafe extern "C" fn on_media(
        _: CFNotificationCenterRef,
        _: *mut c_void,
        _: CFStringRef,
        _: *const c_void,
        _: CFDictionaryRef,
    ) {
        if let Some(tx) = MEDIA_TX.get() {
            let _ = tx.send(HostWake::Media);
        }
    }

    unsafe extern "C" fn on_focus(
        _: CFNotificationCenterRef,
        _: *mut c_void,
        _: CFStringRef,
        _: *const c_void,
        _: CFDictionaryRef,
    ) {
        if let Some(tx) = FOCUS_TX.get() {
            let _ = tx.send(HostWake::Focus);
        }
    }

    unsafe extern "C" fn on_theme(
        _: CFNotificationCenterRef,
        _: *mut c_void,
        _: CFStringRef,
        _: *const c_void,
        _: CFDictionaryRef,
    ) {
        if let Some(tx) = THEME_TX.get() {
            let _ = tx.send(HostWake::Theme);
        }
    }

    fn cfstr(s: &str) -> CFStringRef {
        let c = std::ffi::CString::new(s).expect("cfstr");
        unsafe {
            CFStringCreateWithCString(ptr::null(), c.as_ptr(), K_CF_STRING_ENCODING_UTF8)
        }
    }

    pub fn prefers_dark() -> bool {
        let out = Command::new("defaults")
            .args(["read", "-g", "AppleInterfaceStyle"])
            .output();
        match out {
            Ok(o) if o.status.success() => {
                let s = String::from_utf8_lossy(&o.stdout);
                s.trim().eq_ignore_ascii_case("Dark")
            }
            _ => false,
        }
    }

    pub fn spawn_run_loop() {
        thread::Builder::new()
            .name("tessera-mac-events".into())
            .spawn(|| unsafe {
                let center = CFNotificationCenterGetDistributedCenter();
                if center.is_null() {
                    eprintln!("Tessera: CFNotificationCenter distributed indisponível");
                    return;
                }
                for name in MEDIA_NAMES {
                    CFNotificationCenterAddObserver(
                        center,
                        ptr::null(),
                        Some(on_media),
                        cfstr(name),
                        ptr::null(),
                        CFNotificationSuspensionBehavior::DeliverImmediately,
                    );
                }
                for name in FOCUS_NAMES {
                    CFNotificationCenterAddObserver(
                        center,
                        ptr::null(),
                        Some(on_focus),
                        cfstr(name),
                        ptr::null(),
                        CFNotificationSuspensionBehavior::DeliverImmediately,
                    );
                }
                for name in THEME_NAMES {
                    CFNotificationCenterAddObserver(
                        center,
                        ptr::null(),
                        Some(on_theme),
                        cfstr(name),
                        ptr::null(),
                        CFNotificationSuspensionBehavior::DeliverImmediately,
                    );
                }
                eprintln!("Tessera: mac_events DNC ativo (Music/Spotify/Focus/Theme)");
                let _ = CFRunLoopGetCurrent();
                CFRunLoopRun();
            })
            .expect("spawn mac_events");
    }
}
