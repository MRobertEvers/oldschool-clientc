/*
 * What every page that hosts the client needs before anything else, shared
 * by index.html and compat.html (the compatibility page, which mounts only
 * the canvas): whether this is a phone or tablet, and the soft keyboard.
 * Loaded first, as a plain script.
 */
/*
 * Is this a phone or a tablet? Read once, before anything else, because the
 * client asks it at boot (app.c web_mobile_device): a mobile device is the
 * mobile client -- clienttype 7 (8 on iOS) and on_mobile for the cache's
 * scripts, the same on the login, so the server opens the mobile gameframe --
 * and the page hides the browser's bars for it.
 *
 * In order: ?mobile=1 / ?mobile=0; iOS, whose iPads say "Macintosh" unless
 * they are counted by touch points; the UA client hints' `mobile` (Chrome's
 * own answer, false on tablets); a mobile UA (Android tablets included); and
 * last a touch screen with no fine pointer anywhere (an Android tablet in
 * "desktop site" mode). A touch laptop has a fine pointer and stays desktop.
 */
globalThis.torirsMobileDevice = (() => {
  const forced = new URLSearchParams(window.location.search).get('mobile');
  if (forced !== null) {
    return { mobile: forced !== '0', ios: false, why: '?mobile=' + forced };
  }
  const nav = window.navigator;
  const ua = nav.userAgent || '';
  const touchPoints = nav.maxTouchPoints || 0;
  const media = q => !!(window.matchMedia && window.matchMedia(q).matches);
  if (/iPhone|iPad|iPod/.test(ua)) { return { mobile: true, ios: true, why: 'iOS user agent' }; }
  if (/Macintosh/.test(ua) && touchPoints > 1) {
    return { mobile: true, ios: true, why: 'iPadOS (desktop user agent, touch screen)' };
  }
  if (nav.userAgentData && nav.userAgentData.mobile === true) {
    return { mobile: true, ios: false, why: 'user agent client hints' };
  }
  if (/Android|Mobi|Opera Mini|IEMobile|Silk|Kindle/i.test(ua)) {
    return { mobile: true, ios: false, why: 'mobile user agent' };
  }
  if (touchPoints > 0 && media('(pointer: coarse)') && !media('(any-pointer: fine)')) {
    return { mobile: true, ios: false, why: 'touch screen, no fine pointer' };
  }
  return { mobile: false, ios: false, why: 'desktop' };
})();
document.documentElement.classList.toggle('mobile', globalThis.torirsMobileDevice.mobile);
console.log('[torirs] device: ' + (globalThis.torirsMobileDevice.mobile ? 'mobile' : 'desktop') +
            ' (' + globalThis.torirsMobileDevice.why + ')');

/*
 * The soft keyboard over the hidden input `keyboard` (torirs-keyboard).
 * Defines globalThis.torirsSoftKeyboard, which the client calls.
 */
globalThis.torirsPageKeyboard = function (keyboard) {
  'use strict';
  const mobileDevice = !!(globalThis.torirsMobileDevice && globalThis.torirsMobileDevice.mobile);
    // The soft keyboard: the platform request the client makes when something
    // wants typing (PlatformWindow_SetTextInput -> web_soft_keyboard), the one
    // Android answers through JNI. A phone shows its keyboard for a focused
    // text field, so this focuses a hidden one and hands what is typed into it
    // to the client as keystrokes (torirs_web_text_input / torirs_web_key).
    //
    // What was typed is read as the difference between the field's value and
    // the last one, not from key events: a phone's keyboard composes words and
    // sends Android's keyCode 229 for most of them. The value always starts with
    // one space, so a Backspace with nothing typed still has something to take.
    //
    // iOS raises a keyboard only for a focus made inside a tap, and the request
    // arrives a frame after the tap that caused it: the chatbox's keyboard
    // button is a script the click runs (chatbox_open_input ->
    // MOBILE_KEYBOARDSHOWSTRING). Focusing on the NEXT tap made that button a
    // two-tap toggle whose second tap put the keyboard away again. So on a
    // phone every tap on the canvas focuses the field, inside the tap, with
    // inputmode="none" -- focused, no keyboard -- and the request only changes
    // the inputmode, which raises or lowers the keyboard over a field that
    // already has the focus, with no tap needed. Off the canvas the focus is
    // left alone: the page's own fields and buttons keep their taps.
    //
    // While nothing wants typing the field still has the focus, so its keys
    // go on to SDL like any other (a tablet's hardware keyboard turns the
    // camera) and whatever lands in it is dropped.
    const KEYBOARD_SEED = ' ';
    let keyboardWanted = false;
    let keyboardLast = KEYBOARD_SEED;
    let keyboardComposing = false;
    // The caret goes back after the seed as well: an arrow key from a hardware
    // keyboard, let through while nothing wants typing, leaves it before the
    // seed, where the next letter would read as a Backspace and that letter.
    function keyboardReset() {
      keyboard.value = KEYBOARD_SEED;
      keyboardLast = KEYBOARD_SEED;
      keyboard.setSelectionRange(KEYBOARD_SEED.length, KEYBOARD_SEED.length);
    }
    function keyboardText(text) {
      if (text && window.Module && Module.ccall) { Module.ccall('torirs_web_text_input', null, ['string'], [text]); }
    }
    function keyboardKey(which) {
      if (window.Module && Module.ccall) { Module.ccall('torirs_web_key', null, ['number'], [which]); }
    }
    function keyboardMode() { keyboard.setAttribute('inputmode', keyboardWanted ? 'text' : 'none'); }
    function keyboardFocus() {
      if (document.activeElement === keyboard) { return; }
      keyboardReset();
      keyboardMode();
      keyboard.focus({ preventScroll: true });
    }
    function keyboardTap(e) {
      if (e.target && e.target.id === 'canvas') { keyboardFocus(); }
    }
    keyboard.addEventListener('input', () => {
      if (!keyboardWanted) { keyboardReset(); return; }
      const now = keyboard.value;
      let same = 0;
      while (same < keyboardLast.length && same < now.length && keyboardLast[same] === now[same]) { same++; }
      for (let i = same; i < keyboardLast.length; i++) { keyboardKey(0); }
      keyboardText(now.slice(same).replace(/[\r\n]/g, ''));
      keyboardLast = now;
      if (!now.startsWith(KEYBOARD_SEED) || (now.length > 64 && !keyboardComposing)) { keyboardReset(); }
    });
    keyboard.addEventListener('compositionstart', () => { keyboardComposing = true; });
    keyboard.addEventListener('compositionend', () => { keyboardComposing = false; });
    // While typing is wanted its own keys go no further: the client hears them
    // as the input above, and SDL, listening on the window, would hear them a
    // second time.
    ['keydown', 'keypress', 'keyup'].forEach(type => {
      keyboard.addEventListener(type, e => {
        if (!keyboardWanted) { return; }
        e.stopPropagation();
        if (type === 'keydown' && e.key === 'Enter') {
          e.preventDefault();
          keyboardKey(1);
          keyboardReset();
        }
      });
    });
    // A show re-asked while it is up is how the client brings back a keyboard
    // the person put away (App_TakeTextInputChange pushes again on a request).
    globalThis.torirsSoftKeyboard = on => {
      const was = keyboardWanted;
      keyboardWanted = on && mobileDevice;
      if (!mobileDevice) { return; }
      if (!keyboardWanted) {
        keyboardMode();
        if (document.activeElement === keyboard) { keyboard.blur(); }
        return;
      }
      if (document.activeElement !== keyboard) {
        // Not focused (put away with the keyboard's own Done, or no tap on the
        // canvas yet). Chrome raises it for a focus within a few seconds of the
        // tap; iOS will on the next tap on the canvas, which focuses it.
        keyboardFocus();
        return;
      }
      if (!was) { keyboardReset(); }
      keyboardMode();
      // Chrome raises its keyboard for a focus, not for an inputmode change:
      // focused again, inside the activation the tap left. Never on iOS, where
      // a focus outside the tap is the one that raises nothing.
      if (!globalThis.torirsMobileDevice.ios) {
        keyboard.blur();
        keyboard.focus({ preventScroll: true });
      }
    };
    // Two fingers are the game's: a pinch is its zoom, a two-finger drag its
    // camera (ToriRS_Touch). WebKit -- every iOS browser, Chrome included --
    // takes a pinch for its own page zoom first (gesturestart), and ignores
    // user-scalable=no for that; cancelled here, the touches reach the canvas.
    if (mobileDevice) {
      ['gesturestart', 'gesturechange', 'gestureend'].forEach(type => {
        document.addEventListener(type, e => { e.preventDefault(); }, { passive: false });
      });
      document.addEventListener('touchmove', e => {
        if (e.touches.length > 1) { e.preventDefault(); }
      }, { passive: false });
      document.addEventListener('dblclick', e => { e.preventDefault(); }, { passive: false });
    }

    if (mobileDevice) {
      // Inside the tap: the focus iOS raises a keyboard on later, and a tap on
      // the canvas would otherwise take the focus (and the keyboard) away
      // mid-sentence.
      document.addEventListener('touchend', keyboardTap, { capture: true, passive: true });
    }
};
