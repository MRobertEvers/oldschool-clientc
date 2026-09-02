/*
 * The web chrome executor's page half: chrome commands in, DOM controls out.
 *
 * The wasm client owns the widget MODEL; this file owns the DOM. Commands
 * arrive through window.torirsChromeApply (called from C by EM_JS, see
 * src/ui/torirs_chrome_exec_web.c) and each one creates, updates or destroys a
 * node. What the user does goes back the other way as intents, queued here and
 * pulled one at a time by the client.
 *
 * NOTHING HERE MAKES THE CLIENT WAIT. Every entry point returns immediately;
 * the queue is drained on the client's own frame. That is torirs_channel.js's
 * state rule applied to a second consumer of the same idea -- the renderer must
 * never block on a panel.
 *
 * The look is the same skin the native chrome wears (torirs_chrome_theme_osrs), the
 * same values panel.html already spells in CSS. One extraction, three
 * renderers now.
 */

/*
 * One function wrapped around the file, because these load as plain <script>
 * tags and classic scripts share a single global lexical scope -- this file and
 * torirs_channel.js both declare `INTENT`, which at top level is a SyntaxError
 * that kills the page before the client boots. So the wrapper is the scope, and
 * `global` is the window the exported hooks get hung on. Not a build artifact:
 * there is no build step here, and nothing in this directory is minified.
 */
(function (global) {
  'use strict';

  /* Command kinds — enum ToriRSChromeCmdKind, in order. */
  const CMD = {
    SYNC_BEGIN: 1, SYNC_END: 2,
    PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
    WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
    WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14, WIDGET_SELECTED: 15,
    WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17, WIDGET_OPTION: 18, CHECK_STYLE: 19
  };

  /*
   * Widget kinds — enum ToriRSChromeWidgetKind.
   *
   * These three tables are hand-copies of C enums, and they HAVE gone stale:
   * LISTROW and COLORPICK were added to the widget enum and ACTION to the
   * intent enum without this file hearing about it, so every roster row fell
   * through to the generic branch and every control reported the intent one
   * past the one it meant -- a checkbox toggle arrived as an ACTION, a text
   * edit as a TOGGLE. `web/test/chrome_enum_sync_test.js` now reads the
   * headers and fails if they diverge again.
   */
  const W = {
    LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
    DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
    COLORPICK: 10, TEXTAREA: 11, FREE: 12
  };

  /*
   * A LISTROW's shape bits, carried in WIDGET_ADD's `cw` -- the
   * TORIRS_CHROME_ROW_* defines in torirs_chrome_exec.h. Not an enum, so the
   * sync test above cannot read them; kept beside the tables it does read
   * because they rot the same way.
   */
  const ROW = { ACTION: 0x1, LOCKED: 0x2 };

  /* Intent kinds — enum ToriRSChromeIntentKind. */
  const INTENT = {
    ACTIVATE: 1, ACTION: 2, TOGGLE: 3, TEXT: 4, PICK: 5, TAB: 6, CLOSE: 7
  };

  /*
   * Baked skin slots — enum ToriRSChromeSkinSlot, in order.
   *
   * A fourth hand-copied C enum, and the three above it have all gone stale at
   * least once (see the note on W). `web/test/chrome_enum_sync_test.js` reads
   * the header and fails if this diverges, which is the only thing that has
   * ever kept them honest.
   *
   * Every slot is named whether or not the page draws it -- the test compares
   * the whole enum -- and several are never sent across. CLOSE/CLOSE_OVER are
   * among them: this executor's close is a real DOM <button> wearing the
   * browser's own hover, which is the whole point of a native-widget executor.
   * @see k_web_skin_slots in ui/torirs_chrome_exec_web.c for what ships.
   */
  /*
   * The last four are SYNTHESIZED by the bake rather than lifted from the
   * cache: the close button's own plate with an arrow stamped where the X was
   * (spritebake's --stamp; see TORIRS_CHROME_SKIN_POPOUT). Only this
   * presentation draws them, because only this one has a tab to pop a window
   * into -- but they are slots like any other, and the client sends them the
   * same way.
   *
   * No comments inside the literal below: chrome_enum_sync_test.js reads it as
   * text, and an entry it cannot parse is an error rather than a skip.
   */
  const SKIN = {
    PANEL_BODY: 0,
    SCROLL_UP: 1, SCROLL_DOWN: 2, SCROLL_TRACK: 3,
    SCROLL_GRIP_TOP: 4, SCROLL_GRIP_MID: 5, SCROLL_GRIP_BOTTOM: 6,
    DROPDOWN_BODY: 7, PLUGIN_ICON: 8, CHECK_ON: 9, CHECK_OFF: 10,
    FRAME_TOP_LEFT: 11, FRAME_TOP: 12, FRAME_TOP_RIGHT: 13,
    FRAME_LEFT: 14, FRAME_RIGHT: 15,
    FRAME_BOTTOM_LEFT: 16, FRAME_BOTTOM: 17, FRAME_BOTTOM_RIGHT: 18,
    CLOSE: 19, CLOSE_OVER: 20,
    POPOUT: 21, POPOUT_OVER: 22, DOCK: 23, DOCK_OVER: 24,
    CHECK_BOX_ON: 25, CHECK_BOX_OFF: 26
  };

  /*
   * The frame document's own reset, sent only to a document we made.
   *
   * A window of our own -- the iframe beside the canvas, or the popped-out tab
   * -- holds the chrome and nothing else, so the chrome fills it. That is the
   * same rule ToriRSChromeSync_FillSurface applies to the SDL window, arrived
   * at for the same reason: margins around a panel are what the page behind it
   * shows through, and there is no page behind this one.
   */
  const FRAME_RESET = [
    'html,body{margin:0;padding:0;height:100%;overflow:hidden;background:#5D5447}'
  ].join('');

  /*
   * Layout, at 1x chrome pixels.
   *
   * DEFAULTS ONLY. The real numbers arrive from C at open time
   * (torirsChromeSkinMetrics), out of src/ui/torirs_chrome_metrics.h -- the
   * same table every other presentation lays out from. They
   * are duplicated here so a page running against a client too old to send
   * them still renders something the right shape, and for no other reason: if
   * these and the header ever disagree, the header is right.
   */
  const METRICS = {
    pad: 6, rowH: 18, rowGap: 3, labelW: 104, box: 17, boxSquare: 18, checkGap: 6,
    toggleW: 24, toggleH: 12, rowIcon: 14, rowIconGap: 5, rowNameGap: 4,
    dot: 2, dotPitch: 3, dotInset: 3, scrollW: 16, swatch: 11, swatchGap: 4,
    frame: 6, frameCorner: 32, tabH: 20, tabPadX: 5, fieldPadX: 4,
    fieldInset: 2, dropArrow: 14, dropListPad: 2, dropListRowH: 20,
    dropListRows: 10, textareaRows: 4, textareaLine: 16, textareaPadY: 3
  };

  /*
   * Chrome pixels to CSS pixels.
   *
   * TWO, not one. The game's own interface scaling is what a player runs this
   * at, and a 1x panel -- 18px rows, 17px checkboxes -- is unreadably small in
   * a browser beside page text. Every sprite is drawn `image-rendering:
   * pixelated`, so doubling is the same nearest-neighbour blow-up the game
   * does at interface scale 2 rather than a blur.
   */
  const K = 2;

  /* The CSS variable the attached shell's width is published on -- see
   * ChromeHost.publishDockWidth. */
  const DOCK_WIDTH_VAR = '--torirs-dock-width';

  /*
   * Application-shell geometry, in logical CSS pixels.
   *
   * These are policy rather than widget metrics. The page is split only when
   * the game minimum, the permanent rail, and the minimum useful pane all fit.
   * A plugin never sees this choice and never branches on a browser or device.
   */
  const LAYOUT = {
    railW: 44,
    panelDefault: 320,
    panelMin: 280,
    panelMax: 480,
    gameMin: 765
  };

  /* The palette, from torirs_chrome_metrics.h. Spelled rather than sent: they
   * are colours in a stylesheet, they have never changed, and a CSS file whose
   * every colour is a custom property set from elsewhere cannot be read. */
  const C = {
    body: '#5D5447', chrome: '#000000', text: '#FFFFFF', label: '#FF981F',
    accent: '#FFFF00', on: '#00FF00', fieldBg: '#000000',
    frame: '#0E0E0C', frameInset: '#474745',
    /* ~script7210's own fill for a multiline field. Lighter than fieldBg for
     * the reason the C theme gives: a mostly-empty box at black reads as a
     * hole cut in the panel rather than as a field. */
    textareaBg: '#372E22'
  };

  /*
   * The base stylesheet: the window with NO baked art.
   *
   * Complete on its own. A skin is an enhancement layered over this (see
   * skinStyle), not a requirement -- a client built without the bake, or a
   * page opened before the sprites arrive, gets flat boxes in the right
   * palette at the right size rather than an unstyled form.
   */
  function baseStyle() {
    const m = METRICS;
    function px(n) { return `${n * K}px`; }
    return [
      /*
       * The window's LOOK. Where it goes is `framed` or `floating` below --
       * the two are separate because the same chrome is mounted in three
       * places (an iframe beside the canvas, a popped-out tab, the page
       * itself) and only the placement differs between them.
       */
      /* The one application-owned shell. It stays in normal layout flow; the
       * host page may give its slot a grid track or a flex item, but neither
       * the shell nor the pane is ever absolutely positioned over the game. */
      '.torirs-plugin-shell{display:flex;box-sizing:border-box;width:100%;height:100%;',
      'min-width:0;min-height:0;background:', C.chrome, ';isolation:isolate}',
      '.torirs-plugin-rail{box-sizing:border-box;display:flex;flex:0 0 ',
      LAYOUT.railW, 'px;flex-direction:column;align-items:stretch;padding:', px(2), ';',
      'background:', C.chrome, ';border-right:', px(1), ' solid ', C.frameInset, '}',
      '.torirs-plugin-rail button{box-sizing:border-box;width:100%;min-height:',
      (LAYOUT.railW - 8), 'px;padding:', px(2), ';font:', px(8), '/1 ',
      '"Lucida Console",Menlo,Consolas,monospace;color:', C.label, ';',
      'background:', C.body, ';border:', px(1), ' solid ', C.frameInset, ';cursor:pointer}',
      '.torirs-plugin-rail button[aria-selected="true"]{color:', C.text, ';',
      'border-color:', C.accent, '}',
      '.torirs-plugin-rail button:focus-visible{outline:', px(1), ' solid ', C.accent, ';',
      'outline-offset:', px(1), '}',

      /* `position:relative` is not decoration: an open dropdown list is placed
       * against this box (see placeDropdown), and a static root would hand it
       * to whatever ancestor happened to be positioned. */
      '.torirs-chrome{display:flex;flex-direction:column;box-sizing:border-box;position:relative;',
      'flex:1 1 auto;min-width:0;min-height:0;',
      'background:', C.body, ';border:', px(1), ' solid ', C.chrome, ';color:', C.text, ';',
      /* Sized from the row grid rather than named in points: the rows are
       * 18 chrome pixels and the p12 face they imitate has a 16px line box,
       * so the text has to sit inside that or the row stops being the row. */
      'font:', px(8), '/1 "Lucida Console",Menlo,Consolas,monospace;',
      'text-shadow:', px(1), ' ', px(1), ' 0 rgba(0,0,0,.85);',
      'image-rendering:pixelated}',
      /* Attached or explicitly popped out, it fills the application-owned box
       * and has nothing to float over. */
      '.torirs-chrome.framed{position:relative;width:100%;height:100%;max-height:none;',
      'box-shadow:none}',

      /* The title bar is the minimenu's: the body brown on black, which is
       * what emit_minimenu draws its own heading in. */
      '.torirs-chrome-title{background:', C.chrome, ';color:', C.body, ';font-weight:700;',
      'padding:', px(2), ' ', px(m.pad), ';display:flex;justify-content:space-between;',
      'align-items:center;cursor:default}',
      '.torirs-chrome-title .name{flex:1 1 auto;min-width:0;overflow:hidden;',
      'text-overflow:ellipsis;white-space:nowrap}',
      '.torirs-chrome-title button{background:none;border:0;color:', C.body, ';cursor:pointer;',
      'font:inherit;padding:0 ', px(2), '}',
      '.torirs-chrome-title button:hover{color:', C.accent, '}',
      /* Pop out / put back sits with the other two title-bar verbs. */
      '.torirs-chrome-title button.popout{margin-left:auto}',

      /* A strip of tabs, as the in-canvas chrome draws one: an unselected tab
       * is the body under a veil, the selected one is the body itself with no
       * rule under it joining it to the content below. */
      '.torirs-chrome-tabs{display:flex;gap:', px(1), ';padding:', px(m.pad), ' ', px(m.pad),
      ' 0;border-bottom:', px(1), ' solid ', C.chrome, ';flex-wrap:wrap}',
      '.torirs-chrome-tabs button{height:', px(m.tabH), ';padding:0 ', px(m.tabPadX), ';',
      'background:rgba(0,0,0,.86);border:', px(1), ' solid ', C.chrome, ';border-bottom:0;',
      'color:', C.label, ';cursor:pointer;font:inherit}',
      '.torirs-chrome-tabs button.on{background:transparent;color:', C.text, '}',

      /* The rows take whatever height is left in a framed window and scroll
       * inside it; in a floating one they grow to the cap above.
       *
       * SIDEWAYS as well as down. The window fits itself to its widest row
       * (see fitPanel), so this is not how a row normally gets its width -- it
       * is what happens in the one case the fit cannot reach, a popped-out tab
       * the user has dragged narrower than the rows in it. A row that overflows
       * can then be scrolled to; one that was merely clipped could not. */
      '.torirs-chrome-body{flex:1 1 auto;min-height:0;overflow:auto;padding:', px(m.pad), '}',

      /* One row height for every kind of row, whatever it holds -- see
       * TORIRS_CHROME_M_ROW_H. A column whose rows are each as tall as their
       * own contents does not line its controls up. */
      '.torirs-chrome-row{display:flex;align-items:center;height:', px(m.rowH), ';',
      'margin:0 0 ', px(m.rowGap), '}',
      '.torirs-chrome-row.hidden{display:none}',
      /* The label column is FIXED, not measured: a plugin renaming a setting
       * must not slide every field in the panel sideways. */
      '.torirs-chrome-row>span.lbl{flex:0 0 ', px(m.labelW), ';color:', C.label, ';',
      'overflow:hidden;text-overflow:ellipsis;white-space:nowrap}',

      /* The settings field box: script_3850's, flat. Black under a near-black
       * frame with a grey inset one pixel inside it -- one box shared by a
       * text input, a dropdown and a button, exactly as the reference shares
       * it. */
      '.torirs-chrome-row input[type=text],.torirs-chrome-row span.dd,',
      '.torirs-chrome-row button.act,.torirs-chrome-row button.rowact{',
      'box-sizing:border-box;height:', px(m.rowH), ';background:', C.fieldBg, ';',
      'border:', px(1), ' solid ', C.frame, ';',
      'box-shadow:inset 0 0 0 ', px(1), ' ', C.frameInset, ';',
      'color:', C.text, ';font:inherit;text-shadow:inherit;padding:0 ', px(m.fieldPadX), ';',
      'outline:0;border-radius:0}',
      /*
       * A field shrinks, but not to nothing.
       *
       * At `min-width:0` a row narrower than its label column left the field
       * with the width of its own arrow and no more: the value was gone, and
       * the list it opened was sized to that. The floor is eight characters --
       * `ch` is the advance of "0", which in a monospace face is the advance of
       * everything -- plus the room the arrow is inset in. Below that the row
       * overflows and the body scrolls to it, which is a field you can still
       * read.
       */
      '.torirs-chrome-row input[type=text],.torirs-chrome-row span.dd{flex:1 1 auto;',
      'min-width:calc(8ch + ', px(m.dropArrow + m.fieldInset), ')}',
      '.torirs-chrome-row input[type=text]:focus,.torirs-chrome-row span.dd:focus{',
      'box-shadow:inset 0 0 0 ', px(1), ' ', C.accent, '}',

      /*
       * The dropdown, which is NOT a <select>.
       *
       * It was one, and a <select> is the platform's idiom for the control --
       * but the list a <select> opens is the operating system's, and no page
       * may style it. So the closed button looked like the game and the thing
       * it opened was a macOS menu: the one part of this window the user came
       * to LOOK at was the one part that could not be made to match. The
       * button below is a span and the list is built in the document (see
       * openDropdown), which is the only way a page draws script_9114's list.
       *
       * The button itself is unchanged: the shared field box, its value
       * centred and set in the settings orange, room kept at the right for the
       * arrow the skin puts there.
       */
      '.torirs-chrome-row span.dd{color:', C.label, ';cursor:pointer;display:flex;',
      'align-items:center;justify-content:center;user-select:none;',
      'padding-right:', px(m.dropArrow + m.fieldInset), '}',
      '.torirs-chrome-row span.dd>span.ddval{overflow:hidden;text-overflow:ellipsis;',
      'white-space:nowrap}',

      /*
       * The open list: the box the button wears, hung off its bottom edge and
       * at least as wide as it is, its rows a little taller than a settings row
       * -- the geometry in torirs_chrome_metrics.h, so this list and the one
       * the in-canvas chrome draws are the same list.
       *
       * Placed against the window root rather than the row, because the rows
       * scroll inside an `overflow-y:auto` body and a list inside that would
       * be clipped by it the moment it was longer than the room below.
       */
      '.torirs-chrome-ddlist{position:absolute;z-index:20;box-sizing:border-box;',
      'background:', C.body, ';border:', px(1), ' solid ', C.frame, ';',
      'box-shadow:inset 0 0 0 ', px(1), ' ', C.frameInset, ';',
      'padding:', px(m.dropListPad), ';overflow-y:auto;overflow-x:hidden;',
      'max-height:', px(m.dropListRows * m.dropListRowH + 2 * m.dropListPad), '}',
      /* One row: centred, orange, and banded. The two band values are
       * script_9114's own transparencies (220 and 200 of 255, where 0 is
       * opaque) written as the alpha they come out at, alternating on the
       * OPTION so the stripes scroll with the text. */
      '.torirs-chrome-ddlist .ddrow{box-sizing:border-box;height:',
      px(m.dropListRowH), ';display:flex;align-items:center;justify-content:center;',
      'color:', C.label, ';cursor:pointer;overflow:hidden;white-space:nowrap;',
      'background:rgba(0,0,0,.137)}',
      '.torirs-chrome-ddlist .ddrow:nth-child(even){background:rgba(0,0,0,.216)}',
      /* The cursor thins the veil rather than painting a second colour over
       * it -- the reference picks the row under the pointer out and nothing
       * else, the chosen option included, because the button above the list
       * already says which that is. `.cursor` is the same state reached with
       * the arrow keys. */
      '.torirs-chrome-ddlist .ddrow:hover,.torirs-chrome-ddlist .ddrow.cursor{',
      'background:rgba(0,0,0,.059)}',
      /* A button is the same box with its caption centred, sized to the label
       * column rather than to its own word -- so Save and Revert are one
       * width instead of two. */
      '.torirs-chrome-row button.act{flex:0 0 ', px(m.labelW), ';cursor:pointer;',
      'text-align:center;padding:0 ', px(m.fieldInset), '}',
      '.torirs-chrome-row button.act:hover{box-shadow:inset 0 0 0 ', px(1), ' ', C.accent, ';',
      'color:', C.accent, '}',

      /* A boolean is the interfaces' 17x17 tick/cross. With no skin it is the
       * flat box-and-blob fallback the in-canvas chrome draws. */
      '.torirs-chrome input[type=checkbox]{box-sizing:border-box;flex:0 0 ', px(m.box), ';',
      'width:', px(m.box), ';height:', px(m.box), ';margin:0 ', px(m.checkGap), ' 0 0;',
      'appearance:none;-webkit-appearance:none;background:', C.fieldBg, ';',
      'border:', px(1), ' solid ', C.frameInset, ';cursor:pointer}',
      '.torirs-chrome input[type=checkbox]:checked{background:', C.on, ';',
      'box-shadow:inset 0 0 0 ', px(2), ' ', C.fieldBg, '}',
      /* The other style -- the bordered well (TORIRS_CHROME_CMD_CHECK_STYLE) --
       * is one pixel wider, and a control sized to the wrong art draws it
       * scaled. Sized HERE, in the flat sheet, so the box is right even on a
       * build with no skin: the class says which art the row means, not
       * whether any art arrived. */
      '.torirs-chrome.checkbox-square input[type=checkbox]{flex-basis:', px(m.boxSquare), ';',
      'width:', px(m.boxSquare), ';height:', px(m.boxSquare), '}',
      '.torirs-chrome.checkbox-square .torirs-chrome-row input.rowsw{',
      'background-size:', px(m.boxSquare), ' ', px(m.boxSquare), '}',

      /* The roster row: the name takes the slack, the two controls are pinned
       * right so a column of them lines up however long the names are. */
      '.torirs-chrome-row>span.rowname{flex:1 1 auto;min-width:0;overflow:hidden;',
      'text-overflow:ellipsis;white-space:nowrap;margin-right:', px(m.rowNameGap), '}',
      /* Three dots in a settings-field well. Dots rather than a gear because
       * the game has no gear glyph and the in-canvas chrome draws exactly
       * these three squares. */
      '.torirs-chrome-row button.rowact{flex:0 0 ', px(m.rowIcon), ';',
      'width:', px(m.rowIcon), ';height:', px(m.rowIcon), ';padding:0 0 0 ',
      px(m.dotInset), ';display:flex;align-items:center;cursor:pointer;',
      'gap:', px(m.dotPitch - m.dot), ';margin-right:', px(m.rowIconGap), '}',
      '.torirs-chrome-row button.rowact i{display:block;flex:0 0 ', px(m.dot), ';',
      'width:', px(m.dot), ';height:', px(m.dot), ';background:', C.label, '}',
      '.torirs-chrome-row button.rowact:hover{box-shadow:inset 0 0 0 ', px(1), ' ', C.accent, '}',
      /* The switch: the same tick/cross a checkbox wears, right-aligned in a
       * wider hit box. A sliding switch is an idiom this game does not have. */
      '.torirs-chrome-row input.rowsw{flex:0 0 ', px(m.toggleW), ';width:', px(m.toggleW), ';',
      'margin:0;background-position:right center;background-repeat:no-repeat;',
      'background-size:', px(m.box), ' ', px(m.box), ';border:0}',
      '.torirs-chrome-row input.rowsw:checked{box-shadow:none}',

      '.torirs-chrome-sep{height:', px(m.rowH), ';margin:0 0 ', px(m.rowGap), ';',
      'display:flex;align-items:center}',
      '.torirs-chrome-sep::before{content:"";flex:1;height:', px(1), ';background:', C.chrome, '}',

      /* A colour row is the field box with a swatch inside it at the left --
       * one box, so the column stays straight. */
      '.torirs-chrome-row span.colorpick{flex:1 1 auto;min-width:0;display:flex;',
      'align-items:center;box-sizing:border-box;height:', px(m.rowH), ';',
      'background:', C.fieldBg, ';border:', px(1), ' solid ', C.frame, ';',
      'box-shadow:inset 0 0 0 ', px(1), ' ', C.frameInset, ';padding:0 ', px(m.fieldInset), '}',
      /*
       * A multiline row is the one row that is NOT one rowH tall and not a
       * flex line: its caption sits above its box, full width, because a 104px
       * label column beside a four-line list takes the width the list is for.
       * @see TORIRS_CHROME_W_TEXTAREA.
       */
      '.torirs-chrome-row.textarea{display:block;height:auto}',
      '.torirs-chrome-row.textarea>span.lbl{display:block;flex:none;height:',
      px(m.rowH), ';line-height:', px(m.rowH), '}',
      '.torirs-chrome-row textarea{box-sizing:border-box;display:block;width:100%;',
      'background:', C.textareaBg, ';border:', px(1), ' solid ', C.frame, ';',
      'box-shadow:inset 0 0 0 ', px(1), ' ', C.frameInset, ';color:', C.text, ';',
      'font:inherit;text-shadow:inherit;outline:0;border-radius:0;resize:vertical;',
      'padding:', px(m.textareaPadY), ' ', px(m.fieldPadX), ';',
      'line-height:', px(m.textareaLine), '}',
      '.torirs-chrome-row textarea:focus{box-shadow:inset 0 0 0 ', px(1), ' ', C.accent, '}',

      '.torirs-chrome-row span.colorpick input[type=color]{flex:0 0 ', px(m.swatch), ';',
      'width:', px(m.swatch), ';height:', px(m.swatch), ';padding:0;',
      'background:', C.fieldBg, ';border:', px(1), ' solid ', C.frameInset, ';cursor:pointer}',
      '.torirs-chrome-row input.hex{flex:1 1 auto;min-width:0;border:0;box-shadow:none;',
      'background:transparent;height:', px(m.rowH - 2), ';',
      'margin-left:', px(m.swatchGap), '}',
      '.torirs-chrome-row input.hex:focus{box-shadow:none}',

      '.torirs-chrome-row>div.modelview{flex:1;height:', px(m.rowH), ';',
      'border:', px(1), ' dashed ', C.frameInset, ';color:', C.label, ';',
      'display:flex;align-items:center;justify-content:center;overflow:hidden}'
    ].join('');
  }

  /*
   * The skin layer: the same window, wearing the game's own baked art.
   *
   * Every rule here is an OVERRIDE of something baseStyle already drew flat,
   * so the sheet can be dropped in and taken away without leaving a hole. It
   * is scoped to `.torirs-chrome.skinned`, which is only set once every sprite
   * the sheet names has actually arrived -- a half-skinned window (a frame
   * with no tile behind it) reads as a rendering fault where a flat one reads
   * as a theme.
   */
  function skinStyle(url) {
    const m = METRICS;
    function px(n) { return `${n * K}px`; }
    const tile = `url(${url.tile})`;
    /* The frame's two numbers -- @see the eight-layer rule below -- and the
     * run each rail is stretched along: the box less the corner at each end. */
    const c = m.frameCorner;
    const run = `calc(100% - ${c * 2 * K}px)`;
    /*
     * The title bar's X: the interfaces' own, not the font's.
     *
     * Square and sized to the BAR rather than to the sprite -- 16x16 of art in
     * a 10-chrome-pixel box, the same slight downscale dbg_panel_close_box
     * asks for when it sizes the button to the title's line box less a rule
     * each side. The glyph underneath is not removed, only made transparent:
     * it is what a build with no baked art still shows, and what a screen
     * reader still reads.
     */
    /*
     * Pop out, and put back: one button, two states, four sprites.
     *
     * `.back` is set while the window IS popped out, when the button's job is
     * to bring it home -- so the art it wears then is the arrow pointing the
     * other way. A button that looked the same in both states would be a
     * toggle with no position.
     */
    const popCss = !url.popout ? '' : [
      '.torirs-chrome.skinned .torirs-chrome-title button.popout{width:', px(10), ';',
      'height:', px(10), ';padding:0;margin-right:', px(2), ';color:transparent;',
      'text-shadow:none;background:url(', url.popout, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned .torirs-chrome-title button.popout:hover{color:transparent;',
      'background-image:url(', url.popoutOver, ')}',
      '.torirs-chrome.skinned .torirs-chrome-title button.popout.back{',
      'background-image:url(', url.dock, ')}',
      '.torirs-chrome.skinned .torirs-chrome-title button.popout.back:hover{',
      'background-image:url(', url.dockOver, ')}'
    ].join('');
    const closeCss = !url.close ? '' : [
      '.torirs-chrome.skinned .torirs-chrome-title button.close{width:', px(10), ';',
      'height:', px(10), ';padding:0;color:transparent;text-shadow:none;',
      'background:url(', url.close, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned .torirs-chrome-title button.close:hover{color:transparent;',
      'background-image:url(', url.closeOver, ')}'
    ].join('');
    return [
      closeCss,
      popCss,
      /*
       * Tradebacking behind the panel, and the nine-slice border around it.
       *
       * NOT `border-image`, and that is the whole of this rule's difficulty.
       * The pieces are two sizes -- 32x32 corners carrying an L of 6px rail
       * along their outer edges, and bare 6px rails between them -- and
       * `border-image` slices ONE source into a grid whose corner cells are
       * the border's own width. At the 6px rail this frame is inset by, that
       * samples 6x6 out of each corner and throws the other 26 away: the
       * mitred junction survives, the corner does not. There is no
       * border-image spelling of "32px corners on a 6px rail".
       *
       * So the frame is EIGHT BACKGROUND LAYERS, which is dbg_push_frame
       * verbatim -- each corner at its baked 32, each edge stretched along the
       * run BETWEEN the corners. Corners are listed first because CSS paints
       * the first layer topmost, so an edge that runs under one is covered by
       * it. A panel narrower than two corners clamps the run to zero and wears
       * overlapping corners, which is what the 42px popout strip does with
       * this same art.
       *
       * There is no CENTRE piece: the tile is already under the frame, and a
       * colour painted over the parchment is the frame erasing what it frames.
       *
       * `border-color:transparent` rather than no border at all -- the rail is
       * what the content column is inset by, and the layers are positioned
       * against the BORDER box (background-origin), so the frame is drawn in
       * the strip the border reserves.
       */
      '.torirs-chrome.skinned{border:', px(m.frame), ' solid transparent;background:',
      'url(', url.frameTL, ') no-repeat left top/', px(c), ' ', px(c), ',',
      'url(', url.frameTR, ') no-repeat right top/', px(c), ' ', px(c), ',',
      'url(', url.frameBL, ') no-repeat left bottom/', px(c), ' ', px(c), ',',
      'url(', url.frameBR, ') no-repeat right bottom/', px(c), ' ', px(c), ',',
      'url(', url.frameTop, ') no-repeat ', px(c), ' top/', run, ' ', px(m.frame), ',',
      'url(', url.frameBottom, ') no-repeat ', px(c), ' bottom/', run, ' ', px(m.frame), ',',
      'url(', url.frameLeft, ') no-repeat left ', px(c), '/', px(m.frame), ' ', run, ',',
      'url(', url.frameRight, ') no-repeat right ', px(c), '/', px(m.frame), ' ', run, ',',
      tile, ' repeat;',
      'background-origin:border-box;background-clip:border-box}',

      /* Every field box gets the tile too: the reference tiles graphic_297
       * inside the frame, and a flat black box beside a tiled panel is the one
       * thing that stops this reading as the game's own chrome. */
      '.torirs-chrome.skinned .torirs-chrome-row input[type=text],',
      '.torirs-chrome.skinned .torirs-chrome-row button.act,',
      '.torirs-chrome.skinned .torirs-chrome-row button.rowact,',
      '.torirs-chrome.skinned .torirs-chrome-row span.colorpick{',
      'background:', tile, ' repeat}',

      /* The closed dropdown: the tile, with the scrollbar's own arrow on the
       * right -- which is literally the sprite the reference reuses. Down
       * while the list is shut and up while it is open, the same pair and the
       * same way round as the in-canvas chrome. */
      '.torirs-chrome.skinned .torirs-chrome-row span.dd{',
      'background:url(', url.arrowDown, ') no-repeat right ', px(m.fieldInset), ' center/',
      px(m.dropArrow), ' ', px(m.dropArrow), ',', tile, ' repeat}',
      '.torirs-chrome.skinned .torirs-chrome-row span.dd.open{',
      'background:url(', url.arrowUp, ') no-repeat right ', px(m.fieldInset), ' center/',
      px(m.dropArrow), ' ', px(m.dropArrow), ',', tile, ' repeat}',

      /* A boolean is the sprite, and nothing else: no border, no fill, because
       * the art carries its own ring. */
      '.torirs-chrome.skinned input[type=checkbox]{border:0;box-shadow:none;',
      'background:url(', url.checkOff, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned input[type=checkbox]:checked{',
      'background:url(', url.checkOn, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned .torirs-chrome-row input.rowsw{',
      'background:url(', url.checkOff, ') no-repeat right center/', px(m.box), ' ', px(m.box), '}',
      '.torirs-chrome.skinned .torirs-chrome-row input.rowsw:checked{',
      'background:url(', url.checkOn, ') no-repeat right center/', px(m.box), ' ', px(m.box), '}',

      /* The same four rules again for the OTHER boolean the interfaces draw:
       * the bordered well, at its own 18x18. Both pairs are in the sheet and
       * the class picks -- a style change is then a className toggle rather
       * than a stylesheet rebuild, which matters because the command can
       * arrive at any frame and the sheet is built once. */
      '.torirs-chrome.skinned.checkbox-square input[type=checkbox]{',
      'background:url(', url.checkBoxOff, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned.checkbox-square input[type=checkbox]:checked{',
      'background:url(', url.checkBoxOn, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned.checkbox-square .torirs-chrome-row input.rowsw{',
      'background:url(', url.checkBoxOff, ') no-repeat right center/',
      px(m.boxSquare), ' ', px(m.boxSquare), '}',
      '.torirs-chrome.skinned.checkbox-square .torirs-chrome-row input.rowsw:checked{',
      'background:url(', url.checkBoxOn, ') no-repeat right center/',
      px(m.boxSquare), ' ', px(m.boxSquare), '}',

      /* The open list wears the cache's own lighter parchment -- graphic_1040,
       * not the tradebacking behind the panel -- which is the one thing that
       * says the list is in front of the window rather than part of it. It is
       * a list this page BUILDS now, so this is an ordinary background rather
       * than the <option> hint the browser was free to ignore. */
      '.torirs-chrome.skinned .torirs-chrome-ddlist{',
      'background:url(', url.listTile, ') repeat}',

      /* ~script31's scrollbar: track between two arrow buttons, the grip's
       * middle stretched over its run. WebKit-only, and harmless elsewhere --
       * a browser that ignores these draws its own bar, which is what it did
       * before any of this. */
      '.torirs-chrome.skinned .torirs-chrome-body::-webkit-scrollbar,',
      '.torirs-chrome.skinned .torirs-chrome-ddlist::-webkit-scrollbar{',
      'width:', px(m.scrollW), '}',
      '.torirs-chrome.skinned .torirs-chrome-body::-webkit-scrollbar-track,',
      '.torirs-chrome.skinned .torirs-chrome-ddlist::-webkit-scrollbar-track{',
      'background:url(', url.scrollTrack, ') repeat-y center/100% auto}',
      '.torirs-chrome.skinned .torirs-chrome-body::-webkit-scrollbar-thumb,',
      '.torirs-chrome.skinned .torirs-chrome-ddlist::-webkit-scrollbar-thumb{',
      'background:url(', url.gripMid, ') repeat-y center/100% auto}',
      '.torirs-chrome.skinned .torirs-chrome-body::-webkit-scrollbar-button:vertical:start,',
      '.torirs-chrome.skinned .torirs-chrome-ddlist::-webkit-scrollbar-button:vertical:start{',
      'height:', px(m.scrollW), ';background:url(', url.arrowUp, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned .torirs-chrome-body::-webkit-scrollbar-button:vertical:end,',
      '.torirs-chrome.skinned .torirs-chrome-ddlist::-webkit-scrollbar-button:vertical:end{',
      'height:', px(m.scrollW), ';background:url(', url.arrowDown, ') no-repeat center/100% 100%}'
    ].join('');
  }

  /*
   * The default home is one ordinary DOM subtree in the application's layout:
   * a narrow rail and exactly one live page pane. A wide root gives it a track
   * beside the game; a compact root hides the game track and gives that space
   * to the pane. Neither presentation can cover the canvas because neither is
   * positioned over it.
   *
   * Detaching is an explicit, optional user action. It reparents this SAME DOM
   * subtree; a blocked popup leaves the attached shell untouched.
   */

  /** Find the application regions around the game canvas. */
  function stageAnchor(page) {
    const canvas = page.getElementById ? page.getElementById('canvas') : null;
    let stage;
    let game;
    let layout;
    let app;
    if (!canvas || !canvas.parentNode) return null;
    stage = canvas.parentNode;
    game = (page.getElementById && page.getElementById('game-region')) || stage;
    layout = (page.getElementById && page.getElementById('app-content')) || game.parentNode;
    app = (page.getElementById && page.getElementById('torirs-app')) || layout;
    return { canvas, stage, game, layout, app };
  }

  class ChromeHost {
    constructor() {
      this.root = null;
      this.body = null;
      this.tabsEl = null;
      this.titleEl = null;
      /* The document the live shell currently belongs to: normally the page,
       * or the optional detached tab after an explicit user gesture. */
      this.doc = null;
      /** Stable application slot, the one shell subtree, and its rail. */
      this.slot = null;
      this.slotOwned = false;
      this.shell = null;
      this.railEl = null;
      this.railBtn = null;
      /** The popped-out window when there is one, else null. */
      this.popup = null;
      /** Application regions used to choose split versus exclusive. */
      this.canvas = null;
      this.gameRegion = null;
      this.layoutRoot = null;
      this.appRoot = null;
      this.presentation = 'closed';
      this.panelWidth = LAYOUT.panelDefault;
      this.paneHadFocus = false;
      this.observer = null;
      this.onResize = null;
      /* Panels and widgets by chrome HANDLE, matching the C mirror: a command
       * names a handle, so the lookup should be a property access rather than a
       * search. */
      this.panels = {};
      this.widgets = {};
      this.intents = [];
      /* The panel whose tabs the strip is showing. There is one window, so this
       * is which panel owns it -- not a list. */
      this.tabPanel = -1;
      /* Baked sprites as data: URLs, by slot, and the skin sheet built from
       * them. Both empty on a build that baked none, which is what leaves the
       * window on baseStyle -- see skinDone. */
      this.skin = {};
      this.skinCss = '';
      /* enum ToriRSChromeCheckStyle, as CHECK_STYLE last said: 0 tick/cross,
       * 1 the bordered well. Zero is the model's own default, so a client too
       * old to send the command leaves the window exactly as it was. */
      this.checkStyle = 0;
      /* The dropdown whose list is up, or -1, and the list's node. One window,
       * one open list -- the same rule the model keeps. */
      this.dropOpen = -1;
      this.dropList = null;
      /* The cursor row while the list is being driven from the keyboard; -1
       * means the pointer is the only thing choosing. */
      this.dropCursor = -1;
      /* The listeners an open list owns, so shutting it can take them off
       * again: a dismisser left on the document outlives the list it was for. */
      this.dropOff = null;
      /* The chrome face's advance, measured once per document -- see charW.
       * Zero means "not measured yet", not "zero wide". */
      this.charAdvance = 0;
      /* The widest row the PAGE laid out, in CSS pixels, and the model's own
       * idea of the same thing. @see fitPanel. */
      this.wantWidth = 0;
    }

    /** Acquire or create a normal-flow slot in the application layout. */
    mountAttached(page) {
      const at = stageAnchor(page);
      let slot = page.getElementById && page.getElementById('plugin-chrome-mount');

      if (!page.createElement || !page.body) return null;
      this.slotOwned = false;
      if (!slot) {
        slot = page.createElement('aside');
        slot.className = 'torirs-plugin-chrome-slot';
        slot.setAttribute('aria-label', 'Plugin tools');
        this.slotOwned = true;
        if (at && at.layout) {
          if (at.layout.insertBefore)
            at.layout.insertBefore(slot, at.game.nextSibling || null);
          else
            at.layout.appendChild(slot);
        } else {
          /* A harness without a canvas still gets an ordinary block in
           * document flow. There is no fixed-position overlay fallback. */
          page.body.appendChild(slot);
        }
      }

      if (slot.classList) slot.classList.add('torirs-plugin-chrome-slot');
      slot.hidden = true;
      this.slot = slot;
      this.canvas = at && at.canvas;
      this.gameRegion = at && at.game;
      this.layoutRoot = (at && at.layout) || slot.parentNode || page.body;
      this.appRoot = (at && at.app) || this.layoutRoot;
      this.observeLayout();
      return page;
    }

    /*
     * A tab of its own, opened on demand.
     *
     * about:blank and built by THIS page, not a URL with a script of its own:
     * the widget state lives in the client, the intent queue lives in this
     * host object, and both are one same-origin property access away from the
     * popped-out document. A second page would need the channel, a HELLO, and a
     * second copy of everything below -- for a window whose entire content this
     * file already knows how to build.
     */
    mountPopup() {
      let win;
      let doc;

      if (typeof global.open !== 'function') return null;
      win = global.open('', 'torirs-chrome-plugins', 'width=380,height=620');
      if (!win) return null; /* blocked; the caller keeps what it has */
      doc = win.document;
      if (!doc || !doc.body || !doc.head) { win.close(); return null; }
      doc.title = 'Plugins';
      return { win, doc };
    }

    /** Build the one rail + pane shell around the existing page root. */
    buildShell(doc) {
      const shell = doc.createElement('div');
      const rail = doc.createElement('nav');
      const entry = doc.createElement('button');

      shell.className = 'torirs-plugin-shell';
      shell.setAttribute('role', 'group');
      shell.setAttribute('aria-label', 'Plugin chrome');
      rail.className = 'torirs-plugin-rail';
      rail.setAttribute('aria-label', 'Plugin pages');
      entry.type = 'button';
      entry.className = 'torirs-plugin-rail-entry';
      entry.textContent = 'P';
      entry.title = 'Plugins';
      entry.setAttribute('aria-label', 'Plugins');
      entry.setAttribute('aria-selected', 'true');
      entry.addEventListener('click', () => {
        if (this.tabPanel >= 0)
          this.push({ k: INTENT.CLOSE, p: this.tabPanel, w: -1, v: 0, text: '' });
      });
      rail.appendChild(entry);
      shell.appendChild(rail);
      shell.appendChild(this.root);
      this.shell = shell;
      this.railEl = rail;
      this.railBtn = entry;
      this.isolateInput(shell);
      return shell;
    }

    /** Stop pane input before document/canvas listeners can treat it as game input. */
    isolateInput(shell) {
      const stop = ev => { if (ev && ev.stopPropagation) ev.stopPropagation(); };
      const pointer = [
        'pointerdown', 'pointermove', 'pointerup', 'pointercancel',
        'mousedown', 'mousemove', 'mouseup', 'click',
        'touchstart', 'touchmove', 'touchend', 'wheel', 'contextmenu'
      ];
      const keys = [
        'keydown', 'keyup', 'keypress', 'beforeinput', 'input',
        'compositionstart', 'compositionupdate', 'compositionend'
      ];

      pointer.forEach(type => shell.addEventListener(type, ev => {
        this.paneHadFocus = true;
        stop(ev);
      }));
      keys.forEach(type => shell.addEventListener(type, ev => {
        if (type === 'keydown' && ev && ev.key === 'Escape' && !ev.defaultPrevented) {
          if (this.dropOpen >= 0) this.closeDropdown();
          else if (this.tabPanel >= 0)
            this.push({ k: INTENT.CLOSE, p: this.tabPanel, w: -1, v: 0, text: '' });
          if (ev.preventDefault) ev.preventDefault();
        }
        stop(ev);
      }));
      shell.addEventListener('focusin', ev => {
        this.paneHadFocus = true;
        stop(ev);
      });
      shell.addEventListener('focusout', stop);
    }

    /** Observe element geometry, not only browser-window resize. */
    observeLayout() {
      const seen = [];
      const observe = node => {
        if (!node || seen.indexOf(node) >= 0) return;
        seen.push(node);
        this.observer.observe(node);
      };
      if (typeof global.ResizeObserver === 'function') {
        this.observer = new global.ResizeObserver(() => { this.updateLayout(); });
        observe(this.appRoot);
        observe(this.layoutRoot);
        observe(this.gameRegion);
        observe(this.slot);
      } else if (typeof global.addEventListener === 'function') {
        this.onResize = () => { this.updateLayout(); };
        global.addEventListener('resize', this.onResize);
      }
    }

    unobserveLayout() {
      if (this.observer && this.observer.disconnect) this.observer.disconnect();
      if (this.onResize && typeof global.removeEventListener === 'function')
        global.removeEventListener('resize', this.onResize);
      this.observer = null;
      this.onResize = null;
    }

    measuredWidth() {
      const node = this.appRoot || this.layoutRoot;
      let width = node && node.clientWidth;
      if ((!width || width <= 0) && node && node.getBoundingClientRect)
        width = node.getBoundingClientRect().width;
      if ((!width || width <= 0) && typeof global.innerWidth === 'number')
        width = global.innerWidth;
      /* A host that has not laid out yet starts split and is corrected by the
       * first ResizeObserver delivery. */
      return width > 0 ? width : Number.POSITIVE_INFINITY;
    }

    setLayoutClass(mode) {
      [this.layoutRoot, this.appRoot].forEach(node => {
        if (!node || !node.classList) return;
        node.classList.toggle('torirs-chrome-split', mode === 'split');
        node.classList.toggle('torirs-chrome-exclusive', mode === 'exclusive');
      });
    }

    /** Choose split/exclusive without changing the game's logical canvas. */
    updateLayout() {
      const open = this.tabPanel >= 0 && !!this.panels[this.tabPanel];
      let mode = 'closed';

      if (this.popup) {
        this.presentation = 'detached';
        if (this.shell) this.shell.setAttribute('data-presentation', 'detached');
        if (this.root) this.root.style.width = '';
        this.publishDockWidth();
        return;
      }
      if (open)
        mode = this.measuredWidth() >= LAYOUT.gameMin + LAYOUT.railW + LAYOUT.panelMin
          ? 'split' : 'exclusive';

      this.presentation = mode;
      this.setLayoutClass(mode);
      if (this.slot) this.slot.hidden = !open;
      if (this.gameRegion) this.gameRegion.hidden = mode === 'exclusive';
      if (this.shell) this.shell.setAttribute('data-presentation', mode);
      if (this.slot && this.slot.style) {
        this.slot.style.width = mode === 'split'
          ? `${LAYOUT.railW + this.panelWidth}px`
          : (mode === 'exclusive' ? '100%' : '0px');
        this.slot.style.flex = mode === 'split'
          ? `0 0 ${LAYOUT.railW + this.panelWidth}px`
          : (mode === 'exclusive' ? '1 1 100%' : '0 0 0px');
      }
      if (this.root) this.root.style.width = mode === 'split' ? `${this.panelWidth}px` : '';
      this.publishDockWidth();
    }

    /** Publish attached width for index.html's small full-canvas header. */
    publishDockWidth() {
      const page = global.document;
      const root = page && (page.documentElement || page.body);
      const width = this.presentation === 'split' ? LAYOUT.railW + this.panelWidth : 0;
      if (!root || !root.style || !root.style.setProperty) return;
      root.style.setProperty(DOCK_WIDTH_VAR, `${width}px`);
    }

    restoreGameFocus() {
      const should = this.paneHadFocus;
      this.paneHadFocus = false;
      if (!should || !this.canvas || typeof this.canvas.focus !== 'function') return;
      try { this.canvas.focus({ preventScroll: true }); }
      catch (e) { this.canvas.focus(); }
    }

    /** Take down the one shell, leaving the host page and canvas intact. */
    unmount() {
      this.restoreGameFocus();
      this.setLayoutClass('closed');
      if (this.gameRegion) this.gameRegion.hidden = false;
      if (this.shell && this.shell.parentNode) this.shell.parentNode.removeChild(this.shell);
      this.unobserveLayout();
      if (this.slot) {
        this.slot.hidden = true;
        if (this.slot.style) {
          this.slot.style.width = '0px';
          this.slot.style.flex = '0 0 0px';
        }
      }
      if (this.slotOwned && this.slot && this.slot.parentNode)
        this.slot.parentNode.removeChild(this.slot);
      if (this.popup && !this.popup.closed && this.popup.close) this.popup.close();
      this.popup = null;
      this.doc = null;
      this.slot = null;
      this.slotOwned = false;
      this.shell = null;
      this.railEl = null;
      this.railBtn = null;
      this.canvas = null;
      this.gameRegion = null;
      this.layoutRoot = null;
      this.appRoot = null;
      this.presentation = 'closed';
      this.publishDockWidth();
    }

    open() {
      const page = global.document;
      if (this.root) return true;
      if (!page) return false;

      this.doc = this.mountAttached(page);
      if (!this.doc) return false;
      this.style(this.doc);
      this.root = this.buildRoot(this.doc);
      this.slot.appendChild(this.buildShell(this.doc));
      this.updateLayout();
      this.applySkinClass();
      return true;
    }

    /*
     * Move the window between docked and popped out, keeping the DOM.
     *
     * The nodes are ADOPTED, not rebuilt. Every widget in there was made from a
     * command the client has already sent and will not send again -- the seam
     * emits deltas, so a rebuilt page would sit empty until something in the
     * model happened to change. adoptNode moves the same node objects into the
     * new document, so their listeners, values and caret survive the trip.
     *
     * The new container is made BEFORE the old one comes down: a popup the
     * browser blocks must leave the user with the window they had, not with no
     * window and no way back.
     */
    setPoppedOut(want) {
      const page = global.document;
      const shell = this.shell;
      let target;
      let old;

      if (!shell || !page) return false;
      if (!!this.popup === !!want) return true;

      if (want) {
        target = this.mountPopup();
        if (!target) return false;
        if (shell.parentNode) shell.parentNode.removeChild(shell);
        this.setLayoutClass('closed');
        if (this.gameRegion) this.gameRegion.hidden = false;
        if (this.slot) this.slot.hidden = true;
        this.popup = target.win;
        this.doc = target.doc;
        this.style(this.doc);
        if (this.doc.adoptNode) this.doc.adoptNode(shell);
        this.doc.body.appendChild(shell);
      } else {
        if (!this.slot) return false;
        old = this.popup;
        if (shell.parentNode) shell.parentNode.removeChild(shell);
        this.doc = page;
        this.style(page);
        if (page.adoptNode) page.adoptNode(shell);
        this.slot.appendChild(shell);
        this.popup = null;
        if (old && !old.closed && old.close) old.close();
      }

      this.retagRoot();
      this.applySkinClass();
      this.updateLayout();
      return true;
    }

    /** Attached and detached are application-owned boxes, never overlays. */
    retagRoot() {
      if (!this.root) return;
      this.root.classList.add('framed');
      this.root.classList.remove('floating');
      if (this.popOutBtn) {
        /* The glyph is the unskinned fallback; `back` is what the skin keys its
         * two sprites off. Both move together, so a build with art and one
         * without say the same thing about which way the button now goes. */
        this.popOutBtn.textContent = this.popup ? '\u2913' : '\u2197';
        this.popOutBtn.title = this.popup ? 'Put it back beside the game' : 'Detach into a tab';
        this.popOutBtn.classList.toggle('back', !!this.popup);
      }
    }

    /**
     * The stylesheet, in whichever document is holding the chrome.
     *
     * Written rather than written-once: the sheet's contents change under it
     * when the skin lands (the metrics arrive with the sprites, so even the base
     * sheet is rebuilt), and a popped-out document needs its own copy of
     * whatever the docked one had.
     */
    style(doc) {
      let style = doc.getElementById && doc.getElementById('torirs-chrome-style');
      if (!style) {
        style = doc.createElement('style');
        style.id = 'torirs-chrome-style';
        doc.head.appendChild(style);
      }
      /* The reset goes only to the explicit detached tab. In-page chrome must
       * not restyle the application's html/body. */
      style.textContent =
        (doc === global.document ? '' : FRAME_RESET) + baseStyle() + this.skinCss;
    }

    /* ---- the baked skin -------------------------------------------------------
     *
     * The window wears the game's own art: tradebacking behind the panel and
     * every field, the interfaces' 17x17 tick and cross for a boolean, the
     * scrollbar's arrows and grip, and the nine-slice panel frame. It is the CS2
     * executor's look rebuilt out of DOM nodes, from the same bake, so the two
     * presentations of one window agree.
     *
     * The pixels come across the seam from C (see the note in
     * ui/torirs_chrome_exec_web.c on why they are not shipped beside index.html
     * and not fetched from the cache). Nothing here is required: a client too
     * old to send them, or one built with no bake, never calls skinDone and the
     * flat base sheet is what stays up.
     */

    /**
     * The metrics, from src/ui/torirs_chrome_metrics.h.
     *
     * Overwritten in place rather than replaced, so a client sending a subset --
     * an older one, or a newer key this page does not know -- keeps this file's
     * default for everything it did not name instead of laying out on zeroes.
     */
    skinMetrics(m) {
      if (!m) return;
      for (const key in METRICS) {
        if (!Object.prototype.hasOwnProperty.call(METRICS, key)) continue;
        if (typeof m[key] === 'number' && m[key] > 0) METRICS[key] = m[key];
      }
    }

    /**
     * One baked sprite, as base64 RGBA, turned into a data: URL.
     *
     * Raw pixels rather than a PNG, because encoding one in C would be a codec
     * that exists for this alone -- and the platform already has a decoder: an
     * ImageData onto a canvas, and toDataURL back off it. The bytes arrive
     * R,G,B,A per pixel, which is ImageData's own order; the shuffle out of the
     * bake's 0xAARRGGBB words happens in C, where the word format is known.
     *
     * A document with no canvas -- the node tests' fake one -- simply gets no
     * skin, which is the same path a build with no bake takes.
     */
    skinSprite(slot, w, h, b64) {
      const url = this.decodeSprite(w, h, b64);
      if (url) this.skin[slot] = url;
    }

    decodeSprite(w, h, b64) {
      const doc = this.doc || global.document;
      const need = w * h * 4;
      let canvas;
      let ctx;
      let bin;
      let bytes;

      if (!doc || !doc.createElement || !global.atob || !global.ImageData) return null;
      if (!(w > 0) || !(h > 0)) return null;
      canvas = doc.createElement('canvas');
      if (!canvas || typeof canvas.getContext !== 'function') return null;
      ctx = canvas.getContext('2d');
      if (!ctx || typeof ctx.putImageData !== 'function') return null;

      try {
        bin = global.atob(b64);
      } catch (e) {
        return null;
      }
      if (bin.length < need) return null;
      bytes = new global.Uint8ClampedArray(need);
      for (let i = 0; i < need; i++) bytes[i] = bin.charCodeAt(i);

      canvas.width = w;
      canvas.height = h;
      ctx.putImageData(new global.ImageData(bytes, w, h), 0, 0);
      return canvas.toDataURL();
    }

    /**
     * Every sprite has arrived: build the skin sheet, or stay flat.
     *
     * ALL OR NOTHING. `skinned` goes on only once every URL the sheet names is
     * present, because a half-skinned window -- a nine-slice frame around a flat
     * black panel -- reads as a rendering fault, where the complete flat one
     * reads as a theme.
     */
    skinDone() {
      const url = {
        tile: this.skin[SKIN.PANEL_BODY],
        listTile: this.skin[SKIN.DROPDOWN_BODY],
        checkOn: this.skin[SKIN.CHECK_ON],
        checkOff: this.skin[SKIN.CHECK_OFF],
        arrowUp: this.skin[SKIN.SCROLL_UP],
        arrowDown: this.skin[SKIN.SCROLL_DOWN],
        scrollTrack: this.skin[SKIN.SCROLL_TRACK],
        gripMid: this.skin[SKIN.SCROLL_GRIP_MID],
        /* The eight frame pieces, each named by the layer that draws it. In
         * the required set rather than optional: the frame is the one part of
         * this window that is missed when it is not there, and seven pieces of
         * a border reads as a rendering fault where a flat window reads as a
         * theme. */
        frameTL: this.skin[SKIN.FRAME_TOP_LEFT],
        frameTop: this.skin[SKIN.FRAME_TOP],
        frameTR: this.skin[SKIN.FRAME_TOP_RIGHT],
        frameLeft: this.skin[SKIN.FRAME_LEFT],
        frameRight: this.skin[SKIN.FRAME_RIGHT],
        frameBL: this.skin[SKIN.FRAME_BOTTOM_LEFT],
        frameBottom: this.skin[SKIN.FRAME_BOTTOM],
        frameBR: this.skin[SKIN.FRAME_BOTTOM_RIGHT]
      };
      let key;

      this.skinCss = '';
      for (key in url) {
        if (!Object.prototype.hasOwnProperty.call(url, key)) continue;
        if (!url[key]) { this.applySkinClass(); return; }
      }
      /*
       * The window X is OPTIONAL, and deliberately not in the loop above.
       *
       * A client too old to send it must still get the rest of the skin -- the
       * page and the wasm are versioned separately, which is the same reason the
       * hooks' presence is the availability test. Without it the title bar keeps
       * its text glyph, which is what every build had until the art crossed.
       */
      url.close = this.skin[SKIN.CLOSE] || '';
      /* The pop-out pair, and the pair that puts it back. Optional for the same
       * reason the X is: a client that predates the stamp sends neither, and the
       * title bar keeps its text arrows. */
      url.popout = this.skin[SKIN.POPOUT] || '';
      url.dock = this.skin[SKIN.DOCK] || url.popout;
      /* One button lit from opposite corners: the hover is IN THE ART, exactly
       * as the in-canvas chrome uses it. A build with only the base sprite gets
       * no hover rather than a second, different indication. */
      /* The bordered-well pair, optional for the same reason: a client that
       * predates it sends neither, and a page told to wear the square style by
       * a client that cannot send the art falls back to the tick -- one art
       * everywhere beats a checked box that draws nothing. */
      url.checkBoxOn = this.skin[SKIN.CHECK_BOX_ON] || url.checkOn;
      url.checkBoxOff = this.skin[SKIN.CHECK_BOX_OFF] || url.checkOff;
      url.closeOver = this.skin[SKIN.CLOSE_OVER] || url.close;
      url.popoutOver = this.skin[SKIN.POPOUT_OVER] || url.popout;
      url.dockOver = this.skin[SKIN.DOCK_OVER] || url.dock;

      this.skinCss = skinStyle(url);
      this.applySkinClass();
    }

    /** Push the skin decision into the live document. Separate from skinDone so
     *  a pop-out, which restyles a NEW document, goes through one path. */
    applySkinClass() {
      if (this.doc) this.style(this.doc);
      if (this.root) {
        this.root.classList.toggle('skinned', !!this.skinCss);
        /* Through the SAME path as the skin decision, so a pop-out -- which
         * builds a new root in a new document -- comes up wearing the style
         * this window was told about rather than the default. That is exactly
         * the bug the skin class had before it was pulled out here. */
        this.root.classList.toggle('checkbox-square', this.checkStyle === 1);
      }
    }

    buildRoot(doc) {
      const root = doc.createElement('div');
      const name = doc.createElement('span');
      const pop = doc.createElement('button');
      const close = doc.createElement('button');

      root.className = 'torirs-chrome';

      this.titleEl = doc.createElement('div');
      this.titleEl.className = 'torirs-chrome-title';
      name.className = 'name';
      name.textContent = 'Plugins';

      /*
       * Pop out, and put back.
       *
       * Page-side only: it moves the DOM between containers and the client is
       * never told, because there is nothing here for the model to know. Where a
       * presentation puts its pixels is the presentation's business -- the same
       * reason the SDL window's position and size never reach the model either.
       */
      pop.type = 'button';
      pop.className = 'popout';
      pop.addEventListener('click', () => {
        if (!this.setPoppedOut(!this.popup))
          console.warn('[torirs] the plugin window could not be popped out \u2014 allow popups for this site');
      });
      this.popOutBtn = pop;

      /*
       * The panel's way out, matching what the other presentations offer.
       *
       * ONE button. There was an Ok beside it that committed the page's Save row
       * on the way out; it is gone from every presentation, along with the
       * CONFIRM intent behind it. A page that stages edits carries Save and
       * Revert as labelled rows, which is where a user looks for them.
       *
       * Reported rather than acted on: the MODEL decides whether the window is
       * up, and it hides the panel when it receives this. Taking the DOM down
       * here as well would close it twice and leave the model thinking it is
       * still open.
       */
      close.type = 'button';
      close.className = 'close';
      close.textContent = '\u2715';
      close.title = 'Close';
      close.addEventListener('click', () => {
        this.push({ k: INTENT.CLOSE, p: this.tabPanel, w: -1, v: 0, text: '' });
      });
      this.titleEl.appendChild(name);
      this.titleEl.appendChild(pop);
      this.titleEl.appendChild(close);

      this.tabsEl = doc.createElement('div');
      this.tabsEl.className = 'torirs-chrome-tabs';

      this.body = doc.createElement('div');
      this.body.className = 'torirs-chrome-body';

      root.appendChild(this.titleEl);
      root.appendChild(this.tabsEl);
      root.appendChild(this.body);
      this.root = root;
      this.retagRoot();
      return root;
    }

    setChromeTitle(title) {
      const text = title || 'Plugins';
      if (this.titleEl && this.titleEl.firstChild)
        this.titleEl.firstChild.textContent = text;
      if (this.railBtn) {
        this.railBtn.textContent = String(text).trim().slice(0, 1).toUpperCase() || 'P';
        this.railBtn.title = text;
        this.railBtn.setAttribute('aria-label', text);
        this.railBtn.setAttribute('aria-selected', this.tabPanel >= 0 ? 'true' : 'false');
      }
    }

    /** Remove the old page before another one can build or receive input. */
    clearActivePage(restoreFocus) {
      this.closeDropdown();
      if (this.body) this.body.textContent = '';
      if (this.tabsEl) this.tabsEl.textContent = '';
      this.widgets = {};
      this.panels = {};
      this.tabPanel = -1;
      this.wantWidth = 0;
      this.setChromeTitle('Plugins');
      if (restoreFocus) this.restoreGameFocus();
      this.updateLayout();
    }

    activatePage(panel, title) {
      if (this.tabPanel !== panel) this.clearActivePage(false);
      this.panels = {};
      this.panels[panel] = { tabs: [], activeTab: 0, strip: -1 };
      this.tabPanel = panel;
      this.setChromeTitle(title);
      this.updateLayout();
    }

    close() {
      /* First, so its listeners come off the document that is about to go. */
      this.closeDropdown();
      this.unmount();
      this.root = null;
      this.body = null;
      this.tabsEl = null;
      this.titleEl = null;
      this.popOutBtn = null;
      this.panels = {};
      this.widgets = {};
      this.tabPanel = -1;
      /* The floor belonged to the panel that has gone. Left standing it would
       * be the width of the LAST window every time a narrower one opens. The
       * measured advance is not reset with it: the face is the same one in all
       * three documents this window is built in. */
      this.wantWidth = 0;
    }

    push(intent) {
      /* A listener on a node from the previously selected page may finish
       * after selection changed. Such an intent is stale by definition: only
       * the current page is allowed to receive input. */
      if (intent && intent.p >= 0 && intent.p !== this.tabPanel) return;
      /* Bounded: a page left open behind a client that stopped draining must not
       * grow a queue forever. The oldest go first, because the newest are what
       * the user last did and therefore what they are waiting to see. */
      if (this.intents.length >= 64) this.intents.shift();
      this.intents.push(intent);
    }

    takeIntent() {
      this.checkPopup();
      if (!this.intents.length) return '';
      return JSON.stringify(this.intents.shift());
    }

    /* A detached tab is optional. If the user closes it, adopt the retained
     * shell nodes back into the application slot; plugin/model state does not
     * change merely because an auxiliary browser surface disappeared. */
    checkPopup() {
      const page = global.document;
      if (!this.popup || !this.popup.closed) return;
      this.closeDropdown();
      this.popup = null;
      try {
        if (!page || !this.slot || !this.shell) throw new Error('no attached shell slot');
        if (this.shell.parentNode) this.shell.parentNode.removeChild(this.shell);
        this.doc = page;
        this.style(page);
        if (page.adoptNode) page.adoptNode(this.shell);
        this.slot.appendChild(this.shell);
        this.retagRoot();
        this.applySkinClass();
        this.updateLayout();
      } catch (e) {
        /* A browser that destroys adopted nodes with its tab cannot recover
         * incrementally. Ask the authoritative model to close; its next open
         * will provide a complete command snapshot. */
        this.push({ k: INTENT.CLOSE, p: this.tabPanel, w: -1, v: 0, text: '' });
      }
    }

    /* Which rows are on screen: not hidden, and on the active tab. The one place
     * the two tests are combined, matching ToriRSChromeMirror_Shown. */
    reflow() {
      const panel = this.panels[this.tabPanel];
      const active = panel ? panel.activeTab : 0;
      for (const handle in this.widgets) {
        if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
        const w = this.widgets[handle];
        const shown = !w.hidden && (w.tab < 0 || w.tab === active);
        w.row.classList.toggle('hidden', !shown);
      }
      /* Which rows are up decides how wide the window has to be, so the fit
       * belongs here rather than at each caller: a tab switch changes the
       * widest row as surely as an add does. */
      this.fitPanel();
    }

    /* ---- how wide the window has to be ---------------------------------------
     *
     * MEASURED HERE, not taken from the model.
     *
     * PANEL_RECT carries the width the chrome laid its rows out at, and that
     * number is not this window's width, for two independent reasons.
     *
     * It is in the MODEL's scale -- ui->scale, which follows the display's
     * density and is 1 on an ordinary monitor -- while every metric in this
     * sheet is multiplied by K. A page that took it raw put a 208px label
     * column inside a 240px window, and the fields either side of it collapsed
     * to the width of their own arrow: that is the squashed dropdown this
     * measurement exists to fix.
     *
     * And it was measured in the GAME's face, against advances the browser does
     * not have. Even at matching scales it is a width for a different picture.
     *
     * So the page measures its own rows. The model's number is kept as a FLOOR
     * rather than dropped -- at 2x the two agree, and a floor can only ever ask
     * for more room than the rows need, never less.
     */

    /**
     * The advance of one character in the chrome's face, in CSS pixels.
     *
     * One character is enough because the face is MONOSPACE: `font` names three
     * fixed-pitch families and falls back to the generic one, so a string's
     * width is its length times this. Measured through a canvas, which is the
     * only way a page reads a font's metrics, and cached: the face never
     * changes, and measuring per row would be a text-layout pass per row.
     *
     * The fallback is 0.6em, Lucida Console's own ratio. A document with no
     * canvas -- or a canvas with no 2D context -- then gets a window sized to
     * within a few percent instead of one sized to nothing.
     */
    charW() {
      if (this.charAdvance > 0) return this.charAdvance;
      this.charAdvance = 0.6 * 8 * K;
      try {
        const cv = this.doc && this.doc.createElement && this.doc.createElement('canvas');
        const ctx = cv && cv.getContext && cv.getContext('2d');
        if (ctx && typeof ctx.measureText === 'function') {
          const probe = '0000000000';
          let m;
          ctx.font = `${8 * K}px "Lucida Console",Menlo,Consolas,monospace`;
          m = ctx.measureText(probe);
          if (m && m.width > 0) this.charAdvance = m.width / probe.length;
        }
      } catch (e) {
        /* A canvas that throws is a canvas this page has not got. The 0.6em
         * above already stands, so there is nothing to do but keep it. */
      }
      return this.charAdvance;
    }

    /** `text` set in the chrome's face, in CSS pixels. */
    textW(text) {
      return (text ? String(text).length : 0) * this.charW();
    }

    /**
     * The natural width of one row, in CSS pixels.
     *
     * The shape dbg_widget_width computes for the in-canvas chrome, in the
     * page's own units: a labelled row is the fixed caption column plus its
     * field, and a field is sized to the widest string it will EVER hold rather
     * than the one it holds now -- a dropdown that resized its panel when an
     * option was chosen would move the row out from under the cursor.
     */
    rowWidth(w) {
      const m = METRICS;
      /* A field box's own furniture: the pad either side of the text, and the
       * two rules of the frame around it. */
      const box = 2 * m.fieldPadX * K + 2 * K;
      switch (w.kind) {
        case W.CHECKBOX:
          return (this.checkStyle ? m.boxSquare : m.box) * K + m.checkGap * K +
                 this.textW(w.labelText);
        case W.LISTROW:
          return this.textW(w.labelText) + m.rowNameGap * K +
                 (w.rowAction ? (m.rowIcon + m.rowIconGap) * K : 0) +
                 (w.rowLocked ? 0 : m.toggleW * K);
        case W.DROPDOWN:
          /* The arrow sits inside the box with the field's inset beside it --
           * the room span.dd reserves with its own padding-right. */
          return m.labelW * K + w.optWidest + box + (m.dropArrow + m.fieldInset) * K;
        case W.TEXTINPUT:
          return m.labelW * K + this.textW(w.control && w.control.value) + box;
        case W.COLORPICK:
          /* Seven characters, always: the hex is a fixed width, so the column
           * does not twitch as the value changes under the cursor. */
          return m.labelW * K + 7 * this.charW() + (m.swatch + m.swatchGap) * K + box;
        case W.TEXTAREA:
          /* Wrapped, so there is no width at which it stops wanting one. What
           * it does want is enough room to be worth wrapping into, which is the
           * caption column it does not use spent on the box instead. */
          return m.labelW * K * 2;
        case W.BUTTON:
        case W.MENUITEM:
          /* The label column's width, not the caption's, so a row of buttons is
           * not ragged -- the same rule button.act is sized by. */
          return m.labelW * K;
        case W.TABSTRIP:
        case W.SEPARATOR:
        case W.FREE:
          /* The strip is compressed rather than clipped -- see renderTabs --
           * and the other two have no contents to be wide for. */
          return 0;
        case W.LABEL:
        case W.MODELVIEW:
        default:
          /* Whatever the row put in its one node. A LABEL's string arrives as
           * the command's TEXT and a MODELVIEW's placeholder is written here,
           * so neither is `labelText`. */
          return this.textW(w.control && w.control.textContent);
      }
    }

    /**
     * Size the window to the widest row showing in it.
     *
     * Lands on the attached shell's pane track. A popped-out tab is the user's
     * to size and is left alone, which is the rule PANEL_RECT already kept.
     */
    fitPanel() {
      const m = METRICS;
      let content = 0;

      if (this.popup || !this.root) return;
      for (const handle in this.widgets) {
        if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
        const w = this.widgets[handle];
        let width;
        if (w.row.classList.contains('hidden')) continue;
        width = this.rowWidth(w);
        if (width > content) content = width;
      }
      /* Nothing to fit to. The pane is mounted at its default width before
       * the first command arrives, and a panel that snapped to the minimum in
       * the gap between opening and its first row would be a visible flinch. */
      if (content <= 0 && this.wantWidth <= 0) return;
      if (content > 0) {
        /* The body's own padding, and the bar a long panel grows: a roster that
         * scrolls must not have its switches slide under the scrollbar, which
         * is a control the user can no longer reach. */
        content += 2 * m.pad * K + m.scrollW * K;
      }
      this.setWidth(Math.max(content, this.wantWidth));
    }

    /** The one place pane width is written, using the shared 280..480 policy. */
    setWidth(want) {
      const width = Math.max(LAYOUT.panelMin, Math.min(Math.round(want), LAYOUT.panelMax));
      if (this.panelWidth === width) return;
      this.panelWidth = width;
      this.updateLayout();
      /* An open list hangs off a button that has just moved. */
      if (this.dropOpen >= 0) this.placeDropdown();
    }

    renderTabs() {
      const panel = this.panels[this.tabPanel];
      const doc = this.doc;
      this.tabsEl.textContent = '';
      if (!panel || !panel.tabs || panel.tabs.length < 2) return;
      panel.tabs.forEach((title, index) => {
        const b = doc.createElement('button');
        b.type = 'button';
        b.textContent = title;
        if (index === panel.activeTab) b.className = 'on';
        b.addEventListener('click', () => {
          this.push({ k: INTENT.TAB, p: this.tabPanel, w: panel.strip, v: index, text: '' });
        });
        this.tabsEl.appendChild(b);
      });
    }

    /* ---- the open dropdown list ---------------------------------------------
     *
     * script_9114's list, built in the document, because the list a <select>
     * opens is the operating system's and no page may style it. Everything below
     * exists to make that list behave the way the one it replaced did: it shuts
     * on the next click anywhere else, it takes the keyboard, and it tracks the
     * button it hangs off when the window moves under it.
     */

    toggleDropdown(handle) {
      if (this.dropOpen === handle) this.closeDropdown();
      else this.openDropdown(handle);
    }

    openDropdown(handle) {
      const w = this.widgets[handle];
      const doc = this.doc;
      let list;

      this.closeDropdown();
      if (!w || w.kind !== W.DROPDOWN || !this.root || !doc) return;
      if (!w.options || !w.options.length) return;

      list = doc.createElement('div');
      list.className = 'torirs-chrome-ddlist';
      w.options.forEach((text, index) => {
        const row = doc.createElement('div');
        row.className = 'ddrow';
        row.textContent = text === undefined ? '' : text;
        if (index === w.selected) row.setAttribute('aria-selected', 'true');
        row.addEventListener('mousedown', ev => {
          ev.preventDefault();
          ev.stopPropagation();
          this.pickDropdown(index);
        });
        list.appendChild(row);
      });

      this.root.appendChild(list);
      this.dropList = list;
      this.dropOpen = handle;
      this.dropCursor = w.selected;
      w.control.classList.add('open');
      w.control.setAttribute('aria-expanded', 'true');
      this.placeDropdown();

      /* Open at the chosen row rather than at the top: a list of two thousand
       * loc names that opens on the first of them does not show the user where
       * they already are. */
      if (w.selected >= 0 && list.children[w.selected])
        list.children[w.selected].scrollIntoView({ block: 'nearest' });

      /*
       * What shuts it. A press anywhere else, the rows scrolling under it, the
       * window being resized -- all of them leave the list pointing at nothing,
       * and the press one is what makes the control modal the way every other
       * dropdown in the game is.
       *
       * The press listener is on the DOCUMENT in the CAPTURE phase, and it
       * therefore runs BEFORE the row or the button the press landed on: a
       * `stopPropagation` down there cannot hold it off, and one that tried
       * would shut the list out from under the very click that was choosing a
       * row. So it asks where the press was instead, and lets anything inside
       * this list or on its own button through untouched.
       */
      const dismiss = ev => {
        let node = ev && ev.target;
        while (node) {
          if (node === list || node === w.control) return;
          node = node.parentNode;
        }
        this.closeDropdown();
      };
      doc.addEventListener('mousedown', dismiss, true);
      if (this.body) this.body.addEventListener('scroll', dismiss);
      if (doc.defaultView) doc.defaultView.addEventListener('resize', dismiss);
      this.dropOff = () => {
        doc.removeEventListener('mousedown', dismiss, true);
        if (this.body) this.body.removeEventListener('scroll', dismiss);
        if (doc.defaultView) doc.defaultView.removeEventListener('resize', dismiss);
      };
    }

    closeDropdown() {
      const w = this.widgets[this.dropOpen];

      if (this.dropOff) { this.dropOff(); this.dropOff = null; }
      if (this.dropList && this.dropList.parentNode)
        this.dropList.parentNode.removeChild(this.dropList);
      if (w && w.control) {
        w.control.classList.remove('open');
        w.control.setAttribute('aria-expanded', 'false');
      }
      this.dropList = null;
      this.dropOpen = -1;
      this.dropCursor = -1;
    }

    /**
     * Put the list under its button, or above it when there is no room below.
     *
     * Measured against the window ROOT, which is what the list is positioned
     * in -- the button's own offsetTop is inside a scrolling body and would put
     * the list a scroll's distance away from it.
     */
    placeDropdown() {
      const m = METRICS;
      const w = this.widgets[this.dropOpen];
      const list = this.dropList;
      let root;
      let here;
      let height;
      let below;
      let above;
      let width;
      let left;

      if (!w || !list || !this.root) return;
      root = this.root.getBoundingClientRect();
      here = w.control.getBoundingClientRect();

      /*
       * WIDER than the button when the options need it.
       *
       * The button shows one value and can be narrow enough for it; the list
       * shows every option at once, and one clipped to the button turns
       * "Normal (2 tiles)" into "mal (" -- which is not a choice anybody can
       * make. So it takes the width of its own widest row, and the button's
       * only as a floor, exactly as a native <select> menu does.
       *
       * Capped at the root, because this window is often an IFRAME: a list
       * wider than the document it is in is CUT at the edge, and the rows past
       * the cut are unreachable with nothing on screen to say so. Then pulled
       * back left by however much it overhangs, so the cap is reached by
       * sliding rather than by cropping.
       */
      width = w.optWidest + 2 * m.dropListPad * K + 2 * K;
      /* A list longer than it shows grows a bar of its own, and a bar takes its
       * width out of the rows -- which would clip the very option the width was
       * measured from. */
      if (w.options.length > m.dropListRows) width += m.scrollW * K;
      width = Math.max(here.width, width);
      if (width > root.width && root.width > 0) width = root.width;
      left = here.left - root.left;
      if (left + width > root.width && root.width > 0) left = root.width - width;
      if (left < 0) left = 0;

      list.style.left = `${left}px`;
      list.style.width = `${width}px`;
      list.style.top = `${here.bottom - root.top}px`;
      /* Measured after the width lands: the height depends on how the rows wrap
       * into it, and reading it before is a height for the wrong box. */
      height = list.getBoundingClientRect().height;

      /*
       * Above when there is more room there, and never taller than the room it
       * ends up in.
       *
       * The window is a docked iframe as often as not, and a list that ran past
       * its bottom edge would simply be CUT there -- the rows below the cut are
       * unreachable and there is nothing on screen to say so. Capped instead, so
       * what does not fit scrolls, which is what the bar inside the list is for.
       */
      below = root.bottom - here.bottom;
      above = here.top - root.top;
      if (height > below && above > below) {
        list.style.maxHeight = `${Math.max(above, 2 * m.dropListRowH * K)}px`;
        height = list.getBoundingClientRect().height;
        list.style.top = `${here.top - root.top - height}px`;
      } else if (height > below) {
        list.style.maxHeight = `${Math.max(below, 2 * m.dropListRowH * K)}px`;
      }
    }

    pickDropdown(index) {
      const handle = this.dropOpen;
      const w = this.widgets[handle];

      if (!w || index < 0 || index >= w.options.length) return;
      this.push({
        k: INTENT.PICK, p: w.panel, w: handle, v: index,
        text: w.options[index] === undefined ? '' : w.options[index]
      });
      /* Shut on the way out, as every dropdown in the game does. The value the
       * button shows is the MODEL's answer (WIDGET_SELECTED), not this index --
       * one place decides what the setting became. */
      this.closeDropdown();
    }

    /**
     * The keyboard, which a <select> had for free.
     *
     * Enter and Space open a shut list and choose from an open one; the arrows
     * move a cursor row; Escape shuts it and leaves the setting alone. Losing
     * all of that with the <select> would have been a real regression for
     * anyone who does not reach for the mouse.
     */
    dropdownKey(handle, ev) {
      const w = this.widgets[handle];
      const open = this.dropOpen === handle;
      let step = 0;

      if (!w) return;
      if (ev.key === 'Escape') {
        if (!open) return;
        this.closeDropdown();
        ev.preventDefault();
        return;
      }
      if (ev.key === 'Enter' || ev.key === ' ' || ev.key === 'Spacebar') {
        if (open && this.dropCursor >= 0) this.pickDropdown(this.dropCursor);
        else if (open) this.closeDropdown();
        else this.openDropdown(handle);
        ev.preventDefault();
        return;
      }
      if (ev.key === 'ArrowDown') step = 1;
      else if (ev.key === 'ArrowUp') step = -1;
      else return;

      ev.preventDefault();
      if (!open) { this.openDropdown(handle); return; }
      this.moveDropdownCursor(step);
    }

    moveDropdownCursor(step) {
      const w = this.widgets[this.dropOpen];
      const list = this.dropList;
      let next;

      if (!w || !list || !list.children.length) return;
      next = this.dropCursor + step;
      if (next < 0) next = 0;
      if (next > list.children.length - 1) next = list.children.length - 1;
      for (let i = 0; i < list.children.length; i++)
        list.children[i].classList.toggle('cursor', i === next);
      this.dropCursor = next;
      list.children[next].scrollIntoView({ block: 'nearest' });
    }

    makeWidget(cmd) {
      /* PANEL_OPEN selects the sole live page. Commands for an older visible
       * model panel can still occur in a legacy snapshot, but they must not
       * recreate hidden DOM or become interactive. */
      if (cmd.p !== this.tabPanel || !this.panels[cmd.p]) return;
      /* Everything below is made in the document the chrome is IN -- the
       * attached page's or the popped-out tab's. See ChromeHost::doc. */
      const doc = this.doc;
      const row = doc.createElement('div');
      row.className = 'torirs-chrome-row';

      const entry = {
        row, kind: cmd.v, panel: cmd.p, tab: cmd.tab,
        hidden: false, control: null, options: [],
        /* The caption, kept as a string beside the node that shows it: the
         * window is sized from its rows (see rowWidth) and reading every label
         * back out of the DOM to do it would be a layout read per row. */
        labelText: cmd.label || '',
        /* The widest option a dropdown holds, in CSS pixels, maintained as they
         * arrive rather than by walking the array: a palette's options run to
         * thousands and each one of them arrives as its own command. */
        optWidest: 0
      };

      function labelled(control) {
        if (cmd.label) {
          const lbl = doc.createElement('span');
          lbl.className = 'lbl';
          lbl.textContent = cmd.label;
          row.appendChild(lbl);
        }
        row.appendChild(control);
      }

      switch (cmd.v) {
        case W.CHECKBOX: {
          const box = doc.createElement('input');
          box.type = 'checkbox';
          const text = doc.createElement('span');
          text.textContent = cmd.label || '';
          box.addEventListener('change', () => {
            this.push({ k: INTENT.TOGGLE, p: cmd.p, w: cmd.w, v: box.checked ? 1 : 0, text: '' });
          });
          row.appendChild(box);
          row.appendChild(text);
          entry.control = box;
          entry.labelNode = text;
          break;
        }
        case W.TEXTINPUT: {
          const input = doc.createElement('input');
          input.type = 'text';
          /* The ADD carries a widget's INITIAL strings, and this dropped the
           * value half of them: the sync seeds its shadow from the same
           * command, so no WIDGET_TEXT follows and the field stayed blank
           * until the model changed it. Every other executor already read it
           * off the add (the CS2 one into its own row text, the Win32 one
           * straight into CreateWindowEx). */
          input.value = cmd.text || '';
          /* On change, not on input: an intent per keystroke would send the
           * model a value for every half-typed state, and the chrome's own
           * in-canvas input commits the same way. */
          input.addEventListener('change', () => {
            this.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: input.value });
          });
          labelled(input);
          entry.control = input;
          break;
        }
        case W.TEXTAREA: {
          /*
           * A real <textarea>, which is the platform's idiom for this exactly
           * as <input type=color> is for a colour.
           *
           * It brings its own wrapping, caret, selection and scrollbar, so
           * none of the model's line arithmetic crosses the wall -- and the
           * WIDGET_SELECTED that carries the model's top line for the CS2
           * window is ignored here for the same reason. What the page DOES
           * need from C is the line count, which rides the ADD as `ch`
           * (TORIRS_CHROME_CMD_WIDGET_ADD's `h`).
           */
          const area = doc.createElement('textarea');
          area.rows = cmd.ch > 0 ? cmd.ch : METRICS.textareaRows;
          area.value = cmd.text || '';
          /* A list of item names is not prose; a red underline under every
           * one of them is noise. */
          area.spellcheck = false;
          /* On change, not on input -- the same commit-on-blur every other
           * field here uses, and the same one the in-canvas chrome uses. */
          area.addEventListener('change', () => {
            this.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: area.value });
          });
          row.classList.add('textarea');
          labelled(area);
          entry.control = area;
          break;
        }
        case W.COLORPICK: {
          /*
           * The browser's own colour picker, plus the hex beside it.
           *
           * The MODEL's picker is three HSL16 axis bars, and this page cannot
           * draw them -- but it does not have to: an <input type=color> is the
           * platform's idiom for exactly this control, the way <select> is the
           * platform's idiom for a dropdown. What it gives back is 24-bit RGB,
           * which the model quantises onto a palette entry and echoes back as
           * text -- so the swatch VISIBLY snaps to the colour the renderer can
           * actually produce, which is the honest thing for it to do.
           */
          const swatch = doc.createElement('input');
          const hex = doc.createElement('input');
          const wrap = doc.createElement('span');
          swatch.type = 'color';
          swatch.className = 'swatch';
          hex.type = 'text';
          /* The add's initial value, for the reason the text input's is. */
          hex.value = cmd.text || '';
          if (cmd.text) swatch.value = cmd.text;
          /* Both commit as a TEXT intent, so there is one path into the model
           * and one quantiser -- in C, where the palette is. A second conversion
           * here would be a second place for a colour to land on a different
           * entry than the one the game draws. */
          swatch.addEventListener('change', () => {
            this.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: swatch.value });
          });
          hex.addEventListener('change', () => {
            this.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: hex.value });
          });
          wrap.className = 'colorpick';
          wrap.appendChild(swatch);
          wrap.appendChild(hex);
          labelled(wrap);
          entry.control = hex;
          entry.swatch = swatch;
          break;
        }
        case W.DROPDOWN: {
          /*
           * The button. The LIST is not made here -- it is built when the button
           * is pressed and thrown away when it shuts (see openDropdown), because
           * a panel of twenty dropdowns would otherwise carry twenty lists of
           * every option in each, and a palette dropdown's options run to
           * thousands.
           */
          const dd = doc.createElement('span');
          const val = doc.createElement('span');
          dd.className = 'dd';
          dd.tabIndex = 0;
          dd.setAttribute('role', 'combobox');
          dd.setAttribute('aria-expanded', 'false');
          val.className = 'ddval';
          dd.appendChild(val);
          dd.addEventListener('mousedown', ev => {
            /* On mousedown, and the event stops here: the document-level
             * dismisser below runs on the same event, and a press that opened
             * the list and then dismissed it is a button that does nothing. */
            ev.preventDefault();
            ev.stopPropagation();
            this.toggleDropdown(cmd.w);
          });
          dd.addEventListener('keydown', ev => {
            this.dropdownKey(cmd.w, ev);
          });
          labelled(dd);
          entry.control = dd;
          entry.valueNode = val;
          entry.selected = -1;
          break;
        }
        case W.BUTTON:
        case W.MENUITEM: {
          const button = doc.createElement('button');
          button.type = 'button';
          button.className = 'act';
          button.textContent = cmd.text || cmd.label || '';
          button.addEventListener('click', () => {
            this.push({ k: INTENT.ACTIVATE, p: cmd.p, w: cmd.w, v: 0, text: '' });
          });
          row.appendChild(button);
          entry.control = button;
          break;
        }
        case W.SEPARATOR: {
          row.className = 'torirs-chrome-sep';
          break;
        }
        case W.LISTROW: {
          /*
           * Three zones with two outcomes: the name, an optional settings
           * affordance, and a switch. The affordance and the switch report
           * DIFFERENT intents -- ACTION opens the entry's own page, TOGGLE flips
           * it -- which is the whole reason a roster row is not a checkbox.
           *
           * `cmd.cw` carries the row's SHAPE bits (TORIRS_CHROME_ROW_* in
           * torirs_chrome_exec.h): they ride WIDGET_ADD because whether a row
           * has an action, and whether it can be switched off at all, are part
           * of its shape -- a row that gained or lost either is re-added rather
           * than updated.
           *
           * A LOCKED row is the roster's essential one. It has no second state,
           * so it gets no switch: an unchecked box beside it reads as a plugin
           * somebody turned off, and a checked one as a switch that does nothing
           * when clicked.
           */
          const locked = (cmd.cw & ROW.LOCKED) !== 0;
          const name = doc.createElement('span');
          /* Kept on the entry as well as acted on below: what furniture a row
           * carries is what its natural width is made of. */
          entry.rowLocked = locked;
          entry.rowAction = (cmd.cw & ROW.ACTION) !== 0;
          name.className = 'rowname';
          name.textContent = cmd.label || '';
          row.appendChild(name);

          if (cmd.cw & ROW.ACTION) {
            const action = doc.createElement('button');
            action.type = 'button';
            action.className = 'rowact';
            action.textContent = '\u2026';
            action.title = 'Settings';
            action.addEventListener('click', ev => {
              ev.stopPropagation();
              this.push({ k: INTENT.ACTION, p: cmd.p, w: cmd.w, v: 0, text: '' });
            });
            row.appendChild(action);
          }

          if (locked) {
            entry.labelNode = name;
            break;
          }

          const sw = doc.createElement('input');
          sw.type = 'checkbox';
          sw.className = 'rowsw';
          sw.addEventListener('change', () => {
            this.push({ k: INTENT.TOGGLE, p: cmd.p, w: cmd.w, v: sw.checked ? 1 : 0, text: '' });
          });
          row.appendChild(sw);

          entry.control = sw;
          entry.labelNode = name;
          break;
        }
        case W.MODELVIEW: {
          /*
           * There is no model here to draw.
           *
           * A MODELVIEW is a 3D preview rendered by the client's own scene, and
           * the page has neither the scene nor the geometry -- the command
           * stream carries a widget's shape and text, not its meshes. So this is
           * an honest placeholder that keeps the row's place and says what it
           * would hold, rather than an empty box that reads as a rendering bug.
           *
           * It still takes the focus, which is why it is a real element and not
           * a skipped row: WIDGET_FOCUS is sent for every kind.
           */
          const view = doc.createElement('div');
          view.className = 'modelview';
          view.textContent = cmd.label || 'model preview';
          view.title = 'Model previews are drawn by the client, not the page';
          row.appendChild(view);
          entry.control = view;
          break;
        }
        case W.FREE: {
          /* A recycled slot. It has no appearance and takes no input; the row
           * exists only so the handle still has a node to be removed by. */
          row.classList.add('hidden');
          break;
        }
        case W.TABSTRIP: {
          /* The strip is chrome, not a row: it lives in the header, and its
           * titles arrive as this widget's OPTIONS. The row stays empty and
           * hidden so the handle still has a node to be removed by.
           *
           * Which panel owns the WINDOW is not decided here -- PANEL_OPEN says
           * that, for tabbed and paged panels alike. All this records is which
           * widget the strip's clicks come from. */
          row.classList.add('hidden');
          const panel = this.panels[cmd.p];
          if (panel) panel.strip = cmd.w;
          break;
        }
        default: {
          const span = doc.createElement('span');
          span.textContent = cmd.text || cmd.label || '';
          row.appendChild(span);
          entry.control = span;
          break;
        }
      }

      this.body.appendChild(row);
      this.widgets[cmd.w] = entry;
    }

    apply(cmd) {
      if (!this.root) return;
      const w = this.widgets[cmd.w];
      const doc = this.doc;
      let panel;

      switch (cmd.k) {
        case CMD.CHECK_STYLE:
          /* Chrome-wide: it names no panel and no widget. A class toggle and
           * nothing else -- both styles are already in the sheet, at their own
           * sizes, so every checkbox and every roster switch in the window
           * changes art and box in one repaint. */
          this.checkStyle = cmd.v;
          this.applySkinClass();
          /* The two arts are a pixel apart, which is a pixel every checkbox
           * row's natural width moves by. */
          this.fitPanel();
          break;

        case CMD.PANEL_OPEN:
          /*
           * PANEL_OPEN is also selection in the compatible command protocol.
           * The newest selection clears the former page before any of its rows
           * can be built, leaving one shell, one presenter and one live page.
           */
          this.activatePage(cmd.p, cmd.text);
          break;

        case CMD.PANEL_CLOSE:
          if (this.tabPanel !== cmd.p) break;
          /* A detached page closes back into its application slot first, then
           * the slot collapses. The optional tab never becomes required. */
          if (this.popup) this.setPoppedOut(false);
          this.clearActivePage(true);
          break;

        case CMD.PANEL_TITLE:
          if (cmd.p === this.tabPanel) this.setChromeTitle(cmd.text);
          break;

        case CMD.PANEL_RECT:
          /*
           * The model's box, of which the page honours the WIDTH only.
           *
           * Position and height are deliberately ignored: application layout
           * owns the attached track and the explicit detached tab owns itself.
           * The game canvas's model-space rectangle is never reused for chrome.
           *
           * Width is different: it is the one dimension the model sizes to its
           * CONTENT (the widest row it laid out), so ignoring it is what made a
           * panel of long plugin names sit in a 340px column and ellipsise.
           *
           * A FLOOR rather than the answer, though -- see the note above
           * fitPanel. The number is in the model's scale and measured in the
           * game's face, so the page fits its own rows and takes whichever is
           * wider.
           */
          panel = this.panels[cmd.p];
          if (panel && cmd.cw > 0 && !this.popup) {
            this.wantWidth = cmd.cw + 24;
            this.fitPanel();
          }
          break;

        case CMD.PANEL_TAB:
          panel = this.panels[cmd.p];
          /* The rows under the list are about to be a different set of rows. */
          this.closeDropdown();
          if (panel) { panel.activeTab = cmd.v; this.renderTabs(); this.reflow(); }
          break;

        case CMD.WIDGET_ADD:
          this.makeWidget(cmd);
          this.reflow();
          break;

        case CMD.WIDGET_REMOVE:
          if (w) {
            /* Before the node goes: a list left up would be hanging off a button
             * that no longer exists, and its rows would still take clicks. */
            if (this.dropOpen === cmd.w) this.closeDropdown();
            if (w.row.parentNode) w.row.parentNode.removeChild(w.row);
            delete this.widgets[cmd.w];
            this.fitPanel();
          }
          break;

        case CMD.WIDGET_LABEL:
          if (!w) break;
          w.labelText = cmd.label || '';
          if (w.labelNode) w.labelNode.textContent = cmd.label;
          else {
            const lbl = w.row.querySelector('span.lbl');
            if (lbl) lbl.textContent = cmd.label;
          }
          /* A renamed row can be the widest one, or stop being it. */
          this.fitPanel();
          break;

        case CMD.WIDGET_TEXT:
          if (!w) break;
          if (w.kind === W.COLORPICK) {
            /* The swatch always follows the model -- it holds no caret, so
             * there is nothing to interrupt -- while the hex field is left alone
             * whenever it has the caret, for the same reason a text input is. */
            if (w.swatch) w.swatch.value = cmd.text;
            if (doc.activeElement !== w.control) w.control.value = cmd.text;
          } else if (w.kind === W.TEXTINPUT || w.kind === W.TEXTAREA) {
            /* Never while it has focus: the model is echoing a value the user is
             * still editing, and writing it back would move the caret and undo
             * whatever they typed since the last commit. */
            if (doc.activeElement !== w.control) w.control.value = cmd.text;
          } else if (w.kind === W.DROPDOWN && w.valueNode) {
            w.valueNode.textContent = cmd.text;
          } else if (w.control) {
            w.control.textContent = cmd.text;
          }
          this.fitPanel();
          break;

        case CMD.WIDGET_CHECKED:
          /* LISTROW as well as CHECKBOX: a roster row's switch is a checkbox
           * control and answers the same command. Leaving it out is why the
           * roster rendered every plugin in the same state. */
          if (w && (w.kind === W.CHECKBOX || w.kind === W.LISTROW) && w.control)
            w.control.checked = !!cmd.v;
          break;

        case CMD.WIDGET_HIDDEN:
          if (w) {
            if (cmd.v && this.dropOpen === cmd.w) this.closeDropdown();
            w.hidden = !!cmd.v;
            this.reflow();
          }
          break;

        case CMD.WIDGET_COLOR:
          /*
           * A per-widget text colour override; 0 means "use the theme's".
           *
           * The roster uses it to grey a plugin that failed to load, so dropping
           * it -- which this did -- loses the one signal that a row is not
           * merely switched off.
           */
          if (!w) break;
          const tint = w.labelNode || w.control;
          if (tint && tint.style)
            tint.style.color = cmd.c ? `#${(cmd.c & 0xFFFFFF).toString(16).padStart(6, '0')}` : '';
          break;

        case CMD.WIDGET_FOCUS:
          /*
           * The model says which field it considers focused.
           *
           * A DOM control owns its own focus, so this is only worth acting on
           * when the two have drifted -- the model focusing a field the user did
           * not click, which is how a panel rebuild restores an in-progress
           * edit. Calling focus() on the already-focused element would be
           * harmless; calling it on every widget every frame would not, so it is
           * gated on the mismatch.
           */
          if (!w || !cmd.v || !w.control || !w.control.focus) break;
          if (doc.activeElement !== w.control) w.control.focus();
          break;

        case CMD.WIDGET_OPTIONS:
          if (!w) break;
          w.options = [];
          w.optionsWanted = cmd.v;
          /* The widest is a running maximum over the list that is being thrown
           * away, so it goes with it. */
          w.optWidest = 0;
          /* A list being restated is not the list the open one is showing, and
           * a row index into the old one means nothing against the new. */
          if (w.kind === W.DROPDOWN && this.dropOpen === cmd.w) this.closeDropdown();
          break;

        case CMD.WIDGET_OPTION:
          if (!w) break;
          w.options[cmd.v] = cmd.text;
          if (w.kind === W.TABSTRIP) {
            panel = this.panels[w.panel];
            if (panel) { panel.tabs[cmd.v] = cmd.text; this.renderTabs(); }
          } else if (w.kind === W.DROPDOWN) {
            const ow = this.textW(cmd.text);
            if (ow > w.optWidest) { w.optWidest = ow; this.fitPanel(); }
          }
          /* A DROPDOWN's options are held and nothing else: the rows are built
           * when the list opens, out of exactly this array. */
          break;

        case CMD.WIDGET_SELECTED:
          if (!w) break;
          /* The chosen index is remembered so the next open can put the cursor
           * on it; the VALUE the button shows arrives as WIDGET_TEXT, which is
           * the model's own string for it. */
          if (w.kind === W.DROPDOWN) w.selected = cmd.v;
          /* A COLORPICK's selection is its packed HSL16, and the page has no
           * palette to turn that into pixels. It does not need one: the same
           * change also restates the hex, which is what both controls show. */
          break;

        case CMD.WIDGET_FOCUS:
          /* Deliberately ignored. In the DOM the BROWSER owns focus and the
           * model's copy of it is downstream of what the user clicked here;
           * calling .focus() on the way back would fight the caret the user just
           * placed. The command exists for presentations that have no focus of
           * their own. */
          break;

        default:
          break;
      }
    }
  }

  const host = new ChromeHost();

  /* The hooks C looks for. Their PRESENCE is the availability test -- the
   * client asks before it binds, so a cached page without them degrades to
   * in-canvas chrome instead of a window that silently does nothing. */
  global.torirsChromeOpen = () => host.open();
  global.torirsChromeClose = () => { host.close(); };
  global.torirsChromeApply = cmd => { host.apply(cmd); };
  global.torirsChromeTakeIntent = () => host.takeIntent();
  /* The skin's three, called between open() and the first command. Their
   * absence is not an error on either side: a client that does not send them
   * leaves the window on its flat sheet, and a page that does not define them
   * is simply never called. */
  global.torirsChromeSkinMetrics = m => { host.skinMetrics(m); };
  global.torirsChromeSkinSprite = (slot, w, h, b64) => {
    host.skinSprite(slot, w, h, b64);
  };
  global.torirsChromeSkinDone = () => { host.skinDone(); };

  /* Exported for the node tests, which drive the same host against a fake
   * document -- no browser, no wasm, matching web/test/channel_*.js. */
  if (typeof module !== 'undefined' && module.exports)
    module.exports = { ChromeHost, CMD, W, INTENT, SKIN, METRICS };
})(typeof window !== 'undefined' ? window : globalThis);
