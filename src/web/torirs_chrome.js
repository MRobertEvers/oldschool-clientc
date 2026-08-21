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
(function (global) {
  'use strict';

  /* Command kinds — enum ToriRSChromeCmdKind, in order. */
  var CMD = {
    SYNC_BEGIN: 1, SYNC_END: 2,
    PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
    WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
    WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14, WIDGET_SELECTED: 15,
    WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17, WIDGET_OPTION: 18
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
  var W = {
    LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
    DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
    COLORPICK: 10, FREE: 11
  };

  /* Intent kinds — enum ToriRSChromeIntentKind. */
  var INTENT = {
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
  var SKIN = {
    PANEL_BODY: 0,
    SCROLL_UP: 1, SCROLL_DOWN: 2, SCROLL_TRACK: 3,
    SCROLL_GRIP_TOP: 4, SCROLL_GRIP_MID: 5, SCROLL_GRIP_BOTTOM: 6,
    DROPDOWN_BODY: 7, PLUGIN_ICON: 8, CHECK_ON: 9, CHECK_OFF: 10,
    FRAME_TOP_LEFT: 11, FRAME_TOP: 12, FRAME_TOP_RIGHT: 13,
    FRAME_LEFT: 14, FRAME_RIGHT: 15,
    FRAME_BOTTOM_LEFT: 16, FRAME_BOTTOM: 17, FRAME_BOTTOM_RIGHT: 18,
    CLOSE: 19, CLOSE_OVER: 20
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
  var FRAME_RESET = [
    'html,body{margin:0;padding:0;height:100%;overflow:hidden;background:#5D5447}'
  ].join('');

  /*
   * Layout, at 1x chrome pixels.
   *
   * DEFAULTS ONLY. The real numbers arrive from C at open time
   * (torirsChromeSkinMetrics), out of src/ui/torirs_chrome_metrics.h -- the
   * same table the in-canvas chrome and the CS2 executor lay out from. They
   * are duplicated here so a page running against a client too old to send
   * them still renders something the right shape, and for no other reason: if
   * these and the header ever disagree, the header is right.
   */
  var METRICS = {
    pad: 6, rowH: 18, rowGap: 3, labelW: 104, box: 17, checkGap: 6,
    toggleW: 24, toggleH: 12, rowIcon: 14, rowIconGap: 5, rowNameGap: 4,
    dot: 2, dotPitch: 3, dotInset: 3, scrollW: 16, swatch: 11, swatchGap: 4,
    frame: 3, tabH: 20, tabPadX: 5, fieldPadX: 4, fieldInset: 2, dropArrow: 14,
    dropListPad: 2, dropListRowH: 20, dropListRows: 10
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
  var K = 2;

  /* The CSS variable the docked window's width is published on -- see
   * ChromeHost.publishDockWidth. */
  var DOCK_WIDTH_VAR = '--torirs-dock-width';

  /* The palette, from torirs_chrome_metrics.h. Spelled rather than sent: they
   * are colours in a stylesheet, they have never changed, and a CSS file whose
   * every colour is a custom property set from elsewhere cannot be read. */
  var C = {
    body: '#5D5447', chrome: '#000000', text: '#FFFFFF', label: '#FF981F',
    accent: '#FFFF00', on: '#00FF00', fieldBg: '#000000',
    frame: '#0E0E0C', frameInset: '#474745'
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
    var m = METRICS;
    function px(n) { return (n * K) + 'px'; }
    return [
      /*
       * The window's LOOK. Where it goes is `framed` or `floating` below --
       * the two are separate because the same chrome is mounted in three
       * places (an iframe beside the canvas, a popped-out tab, the page
       * itself) and only the placement differs between them.
       */
      /* `position:relative` is not decoration: an open dropdown list is placed
       * against this box (see placeDropdown), and a static root would hand it
       * to whatever ancestor happened to be positioned -- the page's body in
       * the floating case, which is not where the row is. */
      '.torirs-chrome{display:flex;flex-direction:column;box-sizing:border-box;position:relative;',
      'background:', C.body, ';border:', px(1), ' solid ', C.chrome, ';color:', C.text, ';',
      /* Sized from the row grid rather than named in points: the rows are
       * 18 chrome pixels and the p12 face they imitate has a 16px line box,
       * so the text has to sit inside that or the row stops being the row. */
      'font:', px(8), '/1 "Lucida Console",Menlo,Consolas,monospace;',
      'text-shadow:', px(1), ' ', px(1), ' 0 rgba(0,0,0,.85);',
      'image-rendering:pixelated}',
      /* In a document of its own -- the docked iframe or the popped-out tab --
       * it IS the document: it fills the box the page gave that container and
       * has nothing to float over, so it drops the drop shadow and the corner
       * offsets with it. */
      '.torirs-chrome.framed{position:relative;width:100%;height:100%;max-height:none;',
      'box-shadow:none}',
      /* Over the page, the fallback with no container of its own: pinned to a
       * corner, capped so a long roster scrolls rather than running off the
       * bottom of the viewport. */
      '.torirs-chrome.floating{position:fixed;right:16px;top:16px;width:', px(340), ';',
      'max-height:80vh;z-index:9999;box-shadow:0 6px 24px rgba(0,0,0,.45)}',

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
       * inside it; in a floating one they grow to the cap above. */
      '.torirs-chrome-body{flex:1 1 auto;min-height:0;overflow-y:auto;padding:', px(m.pad), '}',

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
      '.torirs-chrome-row input[type=text],.torirs-chrome-row span.dd{flex:1 1 auto;min-width:0}',
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
       * as wide as it is, its rows a little taller than a settings row -- the
       * geometry in torirs_chrome_metrics.h, so this list and the one the
       * in-canvas chrome draws are the same list.
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
    var m = METRICS;
    function px(n) { return (n * K) + 'px'; }
    var tile = 'url(' + url.tile + ')';
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
    var closeCss = !url.close ? '' : [
      '.torirs-chrome.skinned .torirs-chrome-title button.close{width:', px(10), ';',
      'height:', px(10), ';padding:0;color:transparent;text-shadow:none;',
      'background:url(', url.close, ') no-repeat center/100% 100%}',
      '.torirs-chrome.skinned .torirs-chrome-title button.close:hover{color:transparent;',
      'background-image:url(', url.closeOver, ')}'
    ].join('');
    return [
      closeCss,
      /* Tradebacking behind the panel, and the nine-slice border around it.
       * The frame's own centre piece is never drawn -- `fill` is deliberately
       * absent -- because the tile is already under it and a flat brown
       * painted over the parchment is the frame erasing what it frames. */
      '.torirs-chrome.skinned{background:', tile, ' repeat;border:0;',
      'border-style:solid;border-width:', px(m.frame), ';',
      'border-image:url(', url.frame, ') 3 / ', px(m.frame), ' stretch}',

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

  function ChromeHost() {
    this.root = null;
    this.body = null;
    this.tabsEl = null;
    this.titleEl = null;
    /*
     * The document the chrome is BUILT IN, which is not always the page's.
     *
     * Every DOM call below goes through this rather than the global
     * `document`, because the window can live in three places -- an iframe
     * beside the canvas, a popped-out tab, or the page itself -- and two of
     * those have a document of their own. A stray global `document` is then
     * not a style slip but a node made in the wrong window, which appends
     * nowhere and takes no clicks.
     */
    this.doc = null;
    /** The iframe when docked beside the canvas, else null. */
    this.frame = null;
    /** The popped-out window when there is one, else null. */
    this.popup = null;
    /** The canvas the docked frame matches the height of. */
    this.canvas = null;
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
    /* The dropdown whose list is up, or -1, and the list's node. One window,
     * one open list -- the same rule the model and the CS2 executor keep. */
    this.dropOpen = -1;
    this.dropList = null;
    /* The cursor row while the list is being driven from the keyboard; -1
     * means the pointer is the only thing choosing. */
    this.dropCursor = -1;
    /* The listeners an open list owns, so shutting it can take them off
     * again: a dismisser left on the document outlives the list it was for. */
    this.dropOff = null;
  }

  /*
   * Where the window goes, in order of preference.
   *
   * BESIDE THE CANVAS, IN AN IFRAME. The plugin window is a window, not a
   * heads-up display: it is read while the game is played, so it belongs next
   * to the picture rather than on top of it. Pinned to a corner -- which is
   * what it used to be -- it covered the part of the frame the player was
   * looking at, and every executor that has a real window of its own (SDL,
   * GDI) had already answered this question the other way.
   *
   * An IFRAME rather than a div for the same reason the other executors get
   * a window: the chrome is then in a document of its own, so the page's
   * stylesheet cannot reach its controls, its scrolling is its own, and
   * typing into a settings field cannot reach the canvas's key listeners and
   * walk the player. The height is the canvas's, tracked, so the two read as
   * one object however the page scales the picture.
   *
   * POPPED OUT, when the user asks: the same DOM, moved into a tab of its own.
   *
   * FLOATING over the page, when there is no canvas to sit beside -- a host
   * page that is not index.html, or a document too small to have one. The old
   * behaviour, kept as the fallback rather than as the rule.
   */

  /** The canvas, and the element the window is inserted next to. */
  function stageAnchor(page) {
    var canvas = page.getElementById ? page.getElementById('canvas') : null;
    var anchor;
    if (!canvas || !canvas.parentNode) return null;
    /*
     * Beside the STAGE, not beside the canvas inside it: the page's scaled
     * modes position the canvas absolutely within that stage and centre it
     * there, so a sibling of the canvas would be centred along with it.
     */
    anchor = canvas;
    if (anchor.parentNode !== page.body && anchor.parentNode.parentNode)
      anchor = anchor.parentNode;
    return { canvas: canvas, anchor: anchor, parent: anchor.parentNode };
  }

  /** Make a document of our own inside `frame`, or null if we cannot. */
  function frameDocument(frame) {
    var doc = frame.contentDocument || null;
    if (!doc || !doc.body || !doc.head) return null;
    return doc;
  }

  ChromeHost.prototype.mountFrame = function (page) {
    var at = stageAnchor(page);
    var frame;
    var doc;

    if (!at || !page.createElement) return null;

    frame = page.createElement('iframe');
    frame.className = 'torirs-chrome-frame';
    frame.title = 'Plugins';
    /*
     * No `src`, deliberately.
     *
     * An iframe created without one keeps the initial about:blank document it
     * is born with, and writing into that is synchronous. Setting
     * src="about:blank" instead NAVIGATES it, and the document we built into
     * would be replaced a tick later -- the classic way to lose everything an
     * iframe was filled with.
     */
    frame.style.border = '0';
    frame.style.width = '340px';
    frame.style.flex = '0 0 auto';
    frame.style.alignSelf = 'flex-start';
    frame.style.background = '#5D5447';
    if (at.parent.insertBefore) at.parent.insertBefore(frame, at.anchor.nextSibling || null);
    else at.parent.appendChild(frame);

    doc = frameDocument(frame);
    if (!doc) {
      /* A document we cannot reach is not a window: back out cleanly and let
       * the caller fall through to the floating overlay. */
      if (frame.parentNode) frame.parentNode.removeChild(frame);
      return null;
    }

    this.frame = frame;
    this.canvas = at.canvas;
    this.followCanvas();
    this.publishDockWidth();
    return doc;
  };

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
  ChromeHost.prototype.mountPopup = function () {
    var win;
    var doc;

    if (typeof global.open !== 'function') return null;
    win = global.open('', 'torirs-chrome-plugins', 'width=380,height=620');
    if (!win) return null; /* blocked; the caller keeps what it has */
    doc = win.document;
    if (!doc || !doc.body || !doc.head) { win.close(); return null; }
    doc.title = 'Plugins';
    this.popup = win;
    return doc;
  };

  /** The page itself, the fallback with no window of its own. */
  ChromeHost.prototype.mountFloating = function (page) {
    return page.body ? page : null;
  };

  /*
   * The frame occupies exactly the canvas's rows, tracked.
   *
   * Not set once: the picture's box changes with the page's scaled modes, with
   * the browser window, and with the game's own Display setting (which
   * rewrites the canvas's width/height attributes). A ResizeObserver sees all
   * three because all three end in the canvas's box changing; the resize
   * listener is the fallback for a browser without one, and catches the two
   * that come from the window.
   *
   * The canvas's CONTAINER is watched as well, because the top edge is
   * tracked too and one shape change moves the canvas without resizing it: a
   * page in a scaled mode centres the letterboxed picture inside the stage, so
   * a stage that grows in the dimension the fit is not limited by leaves the
   * canvas the same size lower down the page.
   */
  ChromeHost.prototype.followCanvas = function () {
    var self = this;
    this.matchCanvasBox();
    if (typeof global.ResizeObserver === 'function' && this.canvas) {
      this.observer = new global.ResizeObserver(function () { self.matchCanvasBox(); });
      this.observer.observe(this.canvas);
      /* Never the frame's OWN row, though: our margin is inside that box, so
       * watching it would be watching ourselves. */
      if (this.canvas.parentNode && this.frame &&
          this.canvas.parentNode !== this.frame.parentNode)
        this.observer.observe(this.canvas.parentNode);
    } else if (typeof global.addEventListener === 'function') {
      this.onResize = function () { self.matchCanvasBox(); };
      global.addEventListener('resize', this.onResize);
    }
  };

  ChromeHost.prototype.matchCanvasBox = function () {
    var h = 0;
    if (!this.frame || !this.canvas) return;
    if (this.canvas.getBoundingClientRect)
      h = Math.round(this.canvas.getBoundingClientRect().height || 0);
    /* The backbuffer's height is the honest fallback: it is what the canvas
     * is showing when the page has not scaled it. */
    if (!h) h = this.canvas.height || 0;
    if (h > 0) this.frame.style.height = h + 'px';
    this.matchCanvasTop();
  };

  /*
   * ...and starts on the canvas's top edge, not the row's.
   *
   * The frame is a flex sibling of the STAGE the canvas sits in, so left alone
   * it begins where that row begins. The canvas does not, once the page scales
   * it: it is centred inside the stage and letterboxed, so the picture starts
   * some way down. Matching the height without matching the top is what left
   * the window hanging above the picture it is meant to read as part of.
   *
   * Measured as a DELTA off where the frame is now rather than computed from
   * the parent's box, so the parent's padding, the row's alignment and any
   * border are already in the number, whatever the host page's stylesheet
   * does with them.
   */
  ChromeHost.prototype.matchCanvasTop = function () {
    var mine;
    var theirs;
    var top;

    if (!this.frame || !this.canvas) return;
    if (!this.frame.getBoundingClientRect || !this.canvas.getBoundingClientRect) return;
    mine = this.frame.getBoundingClientRect();
    theirs = this.canvas.getBoundingClientRect();
    /* A picture the page is not showing at all measures as a zero box, and
     * lining up with that would move the window somewhere arbitrary. */
    if (!theirs.height) return;
    top = (parseFloat(this.frame.style.marginTop) || 0) + (theirs.top - mine.top);
    this.frame.style.marginTop = Math.max(0, Math.round(top)) + 'px';
  };

  /*
   * How wide the docked window is, published to the page as a CSS variable.
   *
   * The window sits BESIDE the picture rather than over it -- but the host
   * page has chrome of its own, and index.html pins its view toggles to the
   * top-right corner in full-canvas mode. A corner pinned to the VIEWPORT is
   * this window's title bar. The page cannot ask how wide we are without
   * knowing about this file, so we tell it, and the variable reads 0px
   * whenever nothing is docked -- so a rule that offsets by it needs no other
   * state and no other page needs to care.
   */
  ChromeHost.prototype.publishDockWidth = function () {
    var page = global.document;
    var root = page && (page.documentElement || page.body);
    if (!root || !root.style || !root.style.setProperty) return;
    root.style.setProperty(DOCK_WIDTH_VAR, (this.frame && this.frame.style.width) || '0px');
  };

  ChromeHost.prototype.unfollowCanvas = function () {
    if (this.observer && this.observer.disconnect) this.observer.disconnect();
    if (this.onResize && typeof global.removeEventListener === 'function')
      global.removeEventListener('resize', this.onResize);
    this.observer = null;
    this.onResize = null;
    this.canvas = null;
  };

  /** Take down whichever container is up, leaving `this.root` alone. */
  ChromeHost.prototype.unmount = function () {
    if (this.root && this.root.parentNode) this.root.parentNode.removeChild(this.root);
    this.unfollowCanvas();
    if (this.frame && this.frame.parentNode) this.frame.parentNode.removeChild(this.frame);
    if (this.popup && !this.popup.closed && this.popup.close) this.popup.close();
    this.frame = null;
    this.popup = null;
    this.doc = null;
    this.publishDockWidth();
  };

  ChromeHost.prototype.open = function () {
    var page = global.document;
    if (this.root) return true;
    if (!page) return false;

    this.doc = this.mountFrame(page) || this.mountFloating(page);
    if (!this.doc) return false;
    this.style(this.doc);
    this.root = this.buildRoot(this.doc);
    this.doc.body.appendChild(this.root);
    this.applySkinClass();
    return true;
  };

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
  ChromeHost.prototype.setPoppedOut = function (want) {
    var page = global.document;
    var root = this.root;
    var doc;

    if (!root || !page) return false;
    if (!!this.popup === !!want) return true;

    doc = want ? this.mountPopup() : this.mountFrame(page) || this.mountFloating(page);
    if (!doc) return false;

    if (root.parentNode) root.parentNode.removeChild(root);
    /* Down goes the old one -- but not the popup we may have just made, and
     * not the frame we may have just made, so this is done by hand rather
     * than through unmount(). */
    if (want) {
      this.unfollowCanvas();
      if (this.frame && this.frame.parentNode) this.frame.parentNode.removeChild(this.frame);
      this.frame = null;
      this.publishDockWidth();
    } else if (this.popup) {
      if (!this.popup.closed && this.popup.close) this.popup.close();
      this.popup = null;
    }

    this.doc = doc;
    this.style(doc);
    if (doc.adoptNode) doc.adoptNode(root);
    doc.body.appendChild(root);
    this.retagRoot();
    /* The new document needs the skin CLASS as well as the sheet: the root was
     * adopted, so it kept whatever classes it had, but a document that never
     * saw skinDone would otherwise be styled by a sheet naming a class nothing
     * sets. */
    this.applySkinClass();
    return true;
  };

  /** Which of the three places it is in, as the root's class. */
  ChromeHost.prototype.retagRoot = function () {
    var own = !!(this.frame || this.popup);
    if (!this.root) return;
    this.root.classList.toggle('framed', own);
    this.root.classList.toggle('floating', !own);
    if (this.popOutBtn) {
      this.popOutBtn.textContent = this.popup ? '\u2913' : '\u2197';
      this.popOutBtn.title = this.popup ? 'Put it back beside the game' : 'Pop out into a tab';
    }
  };

  /**
   * The stylesheet, in whichever document is holding the chrome.
   *
   * Written rather than written-once: the sheet's contents change under it
   * when the skin lands (the metrics arrive with the sprites, so even the base
   * sheet is rebuilt), and a popped-out document needs its own copy of
   * whatever the docked one had.
   */
  ChromeHost.prototype.style = function (doc) {
    var style = doc.getElementById && doc.getElementById('torirs-chrome-style');
    if (!style) {
      style = doc.createElement('style');
      style.id = 'torirs-chrome-style';
      doc.head.appendChild(style);
    }
    /* The reset goes only to a document we made. In the page's own document
     * it would restyle the client's html/body. */
    style.textContent =
      (doc === global.document ? '' : FRAME_RESET) + baseStyle() + this.skinCss;
  };

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
  ChromeHost.prototype.skinMetrics = function (m) {
    if (!m) return;
    for (var key in METRICS) {
      if (!Object.prototype.hasOwnProperty.call(METRICS, key)) continue;
      if (typeof m[key] === 'number' && m[key] > 0) METRICS[key] = m[key];
    }
  };

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
  ChromeHost.prototype.skinSprite = function (slot, w, h, b64) {
    var url = this.decodeSprite(w, h, b64);
    if (url) this.skin[slot] = url;
  };

  ChromeHost.prototype.decodeSprite = function (w, h, b64) {
    var doc = this.doc || global.document;
    var need = w * h * 4;
    var canvas;
    var ctx;
    var bin;
    var bytes;

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
    for (var i = 0; i < need; i++) bytes[i] = bin.charCodeAt(i);

    canvas.width = w;
    canvas.height = h;
    ctx.putImageData(new global.ImageData(bytes, w, h), 0, 0);
    return canvas.toDataURL();
  };

  /**
   * The nine frame pieces, composed into ONE 9x9 image.
   *
   * `border-image` takes a single source and slices it, so nine separate 3x3
   * sprites cannot drive it directly. Composed here rather than baked as a
   * tenth sheet, so the bake stays one sprite per cache archive -- which is
   * what makes it a diff a human can check against the cache.
   *
   * The CENTRE cell is left EMPTY on purpose. `border-image` is used without
   * `fill`, so nothing samples the middle; drawing the baked flat brown there
   * would be a flat colour waiting to cover the panel's tile the day somebody
   * adds `fill`, where an empty cell fails visibly instead.
   */
  ChromeHost.prototype.composeFrame = function () {
    var doc = this.doc || global.document;
    var order = [
      SKIN.FRAME_TOP_LEFT, SKIN.FRAME_TOP, SKIN.FRAME_TOP_RIGHT,
      SKIN.FRAME_LEFT, -1, SKIN.FRAME_RIGHT,
      SKIN.FRAME_BOTTOM_LEFT, SKIN.FRAME_BOTTOM, SKIN.FRAME_BOTTOM_RIGHT
    ];
    var canvas;
    var ctx;

    for (var i = 0; i < order.length; i++)
      if (order[i] >= 0 && !this.skin[order[i]]) return null;
    if (!doc || !doc.createElement || typeof global.Image !== 'function') return null;
    canvas = doc.createElement('canvas');
    if (!canvas || typeof canvas.getContext !== 'function') return null;
    ctx = canvas.getContext('2d');
    if (!ctx || typeof ctx.drawImage !== 'function') return null;
    canvas.width = 9;
    canvas.height = 9;

    /* Synchronous by construction: every source is a data: URL this page just
     * produced from a canvas of its own, so it is already decoded and
     * drawImage does not have to wait for a load event. */
    for (var c = 0; c < order.length; c++) {
      if (order[c] < 0) continue;
      try {
        var img = new global.Image();
        img.src = this.skin[order[c]];
        ctx.drawImage(img, (c % 3) * 3, ((c / 3) | 0) * 3);
      } catch (e) {
        return null;
      }
    }
    return canvas.toDataURL();
  };

  /**
   * Every sprite has arrived: build the skin sheet, or stay flat.
   *
   * ALL OR NOTHING. `skinned` goes on only once every URL the sheet names is
   * present, because a half-skinned window -- a nine-slice frame around a flat
   * black panel -- reads as a rendering fault, where the complete flat one
   * reads as a theme.
   */
  ChromeHost.prototype.skinDone = function () {
    var url = {
      tile: this.skin[SKIN.PANEL_BODY],
      listTile: this.skin[SKIN.DROPDOWN_BODY],
      checkOn: this.skin[SKIN.CHECK_ON],
      checkOff: this.skin[SKIN.CHECK_OFF],
      arrowUp: this.skin[SKIN.SCROLL_UP],
      arrowDown: this.skin[SKIN.SCROLL_DOWN],
      scrollTrack: this.skin[SKIN.SCROLL_TRACK],
      gripMid: this.skin[SKIN.SCROLL_GRIP_MID]
    };
    var key;

    this.skinCss = '';
    for (key in url) {
      if (!Object.prototype.hasOwnProperty.call(url, key)) continue;
      if (!url[key]) { this.applySkinClass(); return; }
    }
    url.frame = this.composeFrame();
    if (!url.frame) { this.applySkinClass(); return; }

    /*
     * The window X is OPTIONAL, and deliberately not in the loop above.
     *
     * A client too old to send it must still get the rest of the skin -- the
     * page and the wasm are versioned separately, which is the same reason the
     * hooks' presence is the availability test. Without it the title bar keeps
     * its text glyph, which is what every build had until the art crossed.
     */
    url.close = this.skin[SKIN.CLOSE] || '';
    /* One button lit from opposite corners: the hover is IN THE ART, exactly
     * as the in-canvas chrome uses it. A build with only the base sprite gets
     * no hover rather than a second, different indication. */
    url.closeOver = this.skin[SKIN.CLOSE_OVER] || url.close;

    this.skinCss = skinStyle(url);
    this.applySkinClass();
  };

  /** Push the skin decision into the live document. Separate from skinDone so
   *  a pop-out, which restyles a NEW document, goes through one path. */
  ChromeHost.prototype.applySkinClass = function () {
    if (this.doc) this.style(this.doc);
    if (this.root) this.root.classList.toggle('skinned', !!this.skinCss);
  };

  ChromeHost.prototype.buildRoot = function (doc) {
    var self = this;
    var root = doc.createElement('div');
    var name = doc.createElement('span');
    var pop = doc.createElement('button');
    var close = doc.createElement('button');

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
    pop.addEventListener('click', function () {
      if (!self.setPoppedOut(!self.popup))
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
    close.addEventListener('click', function () {
      self.push({ k: INTENT.CLOSE, p: self.tabPanel, w: -1, v: 0, text: '' });
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
  };

  ChromeHost.prototype.close = function () {
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
  };

  ChromeHost.prototype.push = function (intent) {
    /* Bounded: a page left open behind a client that stopped draining must not
     * grow a queue forever. The oldest go first, because the newest are what
     * the user last did and therefore what they are waiting to see. */
    if (this.intents.length >= 64) this.intents.shift();
    this.intents.push(intent);
  };

  ChromeHost.prototype.takeIntent = function () {
    this.checkPopup();
    if (!this.intents.length) return '';
    return JSON.stringify(this.intents.shift());
  };

  /*
   * The popped-out tab, closed from its own title bar.
   *
   * There is no event for it worth trusting -- `unload` in the popup fires
   * during a reload too -- so it is polled, here, because the client already
   * calls this once a frame and a timer would be a second clock for one bit.
   *
   * Reported as a CLOSE, exactly as the SDL executor reports its window's X:
   * the presentation is gone, the model is what decides whether the window is
   * up, and the host takes the executor down on the answer. The DOM went with
   * the tab, so `root` is dropped here rather than removed -- there is nothing
   * left to remove it from.
   */
  ChromeHost.prototype.checkPopup = function () {
    if (!this.popup || !this.popup.closed) return;
    /* The tab took the list's document with it, so there is nothing to remove
     * the node from -- but the state has to be dropped or the next open finds
     * a `dropOpen` naming a widget that no longer exists. */
    this.dropList = null;
    this.dropOff = null;
    this.closeDropdown();
    this.popup = null;
    this.root = null;
    this.body = null;
    this.tabsEl = null;
    this.titleEl = null;
    this.popOutBtn = null;
    this.widgets = {};
    this.doc = null;
    this.push({ k: INTENT.CLOSE, p: this.tabPanel, w: -1, v: 0, text: '' });
    this.panels = {};
    this.tabPanel = -1;
  };

  /* Which rows are on screen: not hidden, and on the active tab. The one place
   * the two tests are combined, matching ToriRSChromeMirror_Shown. */
  ChromeHost.prototype.reflow = function () {
    var panel = this.panels[this.tabPanel];
    var active = panel ? panel.activeTab : 0;
    for (var handle in this.widgets) {
      if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
      var w = this.widgets[handle];
      var shown = !w.hidden && (w.tab < 0 || w.tab === active);
      w.row.classList.toggle('hidden', !shown);
    }
  };

  ChromeHost.prototype.renderTabs = function () {
    var panel = this.panels[this.tabPanel];
    var self = this;
    var doc = this.doc;
    this.tabsEl.textContent = '';
    if (!panel || !panel.tabs || panel.tabs.length < 2) return;
    panel.tabs.forEach(function (title, index) {
      var b = doc.createElement('button');
      b.type = 'button';
      b.textContent = title;
      if (index === panel.activeTab) b.className = 'on';
      b.addEventListener('click', function () {
        self.push({ k: INTENT.TAB, p: self.tabPanel, w: panel.strip, v: index, text: '' });
      });
      self.tabsEl.appendChild(b);
    });
  };

  /* ---- the open dropdown list ---------------------------------------------
   *
   * script_9114's list, built in the document, because the list a <select>
   * opens is the operating system's and no page may style it. Everything below
   * exists to make that list behave the way the one it replaced did: it shuts
   * on the next click anywhere else, it takes the keyboard, and it tracks the
   * button it hangs off when the window moves under it.
   */

  ChromeHost.prototype.toggleDropdown = function (handle) {
    if (this.dropOpen === handle) this.closeDropdown();
    else this.openDropdown(handle);
  };

  ChromeHost.prototype.openDropdown = function (handle) {
    var self = this;
    var w = this.widgets[handle];
    var doc = this.doc;
    var list;

    this.closeDropdown();
    if (!w || w.kind !== W.DROPDOWN || !this.root || !doc) return;
    if (!w.options || !w.options.length) return;

    list = doc.createElement('div');
    list.className = 'torirs-chrome-ddlist';
    w.options.forEach(function (text, index) {
      var row = doc.createElement('div');
      row.className = 'ddrow';
      row.textContent = text === undefined ? '' : text;
      if (index === w.selected) row.setAttribute('aria-selected', 'true');
      row.addEventListener('mousedown', function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        self.pickDropdown(index);
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
    var dismiss = function (ev) {
      var node = ev && ev.target;
      while (node) {
        if (node === list || node === w.control) return;
        node = node.parentNode;
      }
      self.closeDropdown();
    };
    doc.addEventListener('mousedown', dismiss, true);
    if (this.body) this.body.addEventListener('scroll', dismiss);
    if (doc.defaultView) doc.defaultView.addEventListener('resize', dismiss);
    this.dropOff = function () {
      doc.removeEventListener('mousedown', dismiss, true);
      if (self.body) self.body.removeEventListener('scroll', dismiss);
      if (doc.defaultView) doc.defaultView.removeEventListener('resize', dismiss);
    };
  };

  ChromeHost.prototype.closeDropdown = function () {
    var w = this.widgets[this.dropOpen];

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
  };

  /**
   * Put the list under its button, or above it when there is no room below.
   *
   * Measured against the window ROOT, which is what the list is positioned
   * in -- the button's own offsetTop is inside a scrolling body and would put
   * the list a scroll's distance away from it.
   */
  ChromeHost.prototype.placeDropdown = function () {
    var m = METRICS;
    var w = this.widgets[this.dropOpen];
    var list = this.dropList;
    var root;
    var here;
    var height;
    var below;
    var above;

    if (!w || !list || !this.root) return;
    root = this.root.getBoundingClientRect();
    here = w.control.getBoundingClientRect();
    list.style.left = (here.left - root.left) + 'px';
    list.style.width = here.width + 'px';
    list.style.top = (here.bottom - root.top) + 'px';
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
      list.style.maxHeight = Math.max(above, 2 * m.dropListRowH * K) + 'px';
      height = list.getBoundingClientRect().height;
      list.style.top = (here.top - root.top - height) + 'px';
    } else if (height > below) {
      list.style.maxHeight = Math.max(below, 2 * m.dropListRowH * K) + 'px';
    }
  };

  ChromeHost.prototype.pickDropdown = function (index) {
    var handle = this.dropOpen;
    var w = this.widgets[handle];

    if (!w || index < 0 || index >= w.options.length) return;
    this.push({
      k: INTENT.PICK, p: w.panel, w: handle, v: index,
      text: w.options[index] === undefined ? '' : w.options[index]
    });
    /* Shut on the way out, as every dropdown in the game does. The value the
     * button shows is the MODEL's answer (WIDGET_SELECTED), not this index --
     * one place decides what the setting became. */
    this.closeDropdown();
  };

  /**
   * The keyboard, which a <select> had for free.
   *
   * Enter and Space open a shut list and choose from an open one; the arrows
   * move a cursor row; Escape shuts it and leaves the setting alone. Losing
   * all of that with the <select> would have been a real regression for
   * anyone who does not reach for the mouse.
   */
  ChromeHost.prototype.dropdownKey = function (handle, ev) {
    var w = this.widgets[handle];
    var open = this.dropOpen === handle;
    var step = 0;

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
  };

  ChromeHost.prototype.moveDropdownCursor = function (step) {
    var w = this.widgets[this.dropOpen];
    var list = this.dropList;
    var next;

    if (!w || !list || !list.children.length) return;
    next = this.dropCursor + step;
    if (next < 0) next = 0;
    if (next > list.children.length - 1) next = list.children.length - 1;
    for (var i = 0; i < list.children.length; i++)
      list.children[i].classList.toggle('cursor', i === next);
    this.dropCursor = next;
    list.children[next].scrollIntoView({ block: 'nearest' });
  };

  ChromeHost.prototype.makeWidget = function (cmd) {
    var self = this;
    /* Everything below is made in the document the chrome is IN -- the
     * iframe's, the popped-out tab's, or the page's. See ChromeHost::doc. */
    var doc = this.doc;
    var row = doc.createElement('div');
    row.className = 'torirs-chrome-row';

    var entry = {
      row: row, kind: cmd.v, panel: cmd.p, tab: cmd.tab,
      hidden: false, control: null, options: []
    };

    function labelled(control) {
      if (cmd.label) {
        var lbl = doc.createElement('span');
        lbl.className = 'lbl';
        lbl.textContent = cmd.label;
        row.appendChild(lbl);
      }
      row.appendChild(control);
    }

    switch (cmd.v) {
      case W.CHECKBOX: {
        var box = doc.createElement('input');
        box.type = 'checkbox';
        var text = doc.createElement('span');
        text.textContent = cmd.label || '';
        box.addEventListener('change', function () {
          self.push({ k: INTENT.TOGGLE, p: cmd.p, w: cmd.w, v: box.checked ? 1 : 0, text: '' });
        });
        row.appendChild(box);
        row.appendChild(text);
        entry.control = box;
        entry.labelNode = text;
        break;
      }
      case W.TEXTINPUT: {
        var input = doc.createElement('input');
        input.type = 'text';
        /* On change, not on input: an intent per keystroke would send the
         * model a value for every half-typed state, and the chrome's own
         * in-canvas input commits the same way. */
        input.addEventListener('change', function () {
          self.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: input.value });
        });
        labelled(input);
        entry.control = input;
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
        var swatch = doc.createElement('input');
        var hex = doc.createElement('input');
        var wrap = doc.createElement('span');
        swatch.type = 'color';
        swatch.className = 'swatch';
        hex.type = 'text';
        /* Both commit as a TEXT intent, so there is one path into the model
         * and one quantiser -- in C, where the palette is. A second conversion
         * here would be a second place for a colour to land on a different
         * entry than the one the game draws. */
        swatch.addEventListener('change', function () {
          self.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: swatch.value });
        });
        hex.addEventListener('change', function () {
          self.push({ k: INTENT.TEXT, p: cmd.p, w: cmd.w, v: 0, text: hex.value });
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
        var dd = doc.createElement('span');
        var val = doc.createElement('span');
        dd.className = 'dd';
        dd.tabIndex = 0;
        dd.setAttribute('role', 'combobox');
        dd.setAttribute('aria-expanded', 'false');
        val.className = 'ddval';
        dd.appendChild(val);
        dd.addEventListener('mousedown', function (ev) {
          /* On mousedown, and the event stops here: the document-level
           * dismisser below runs on the same event, and a press that opened
           * the list and then dismissed it is a button that does nothing. */
          ev.preventDefault();
          ev.stopPropagation();
          self.toggleDropdown(cmd.w);
        });
        dd.addEventListener('keydown', function (ev) {
          self.dropdownKey(cmd.w, ev);
        });
        labelled(dd);
        entry.control = dd;
        entry.valueNode = val;
        entry.selected = -1;
        break;
      }
      case W.BUTTON:
      case W.MENUITEM: {
        var button = doc.createElement('button');
        button.type = 'button';
        button.className = 'act';
        button.textContent = cmd.text || cmd.label || '';
        button.addEventListener('click', function () {
          self.push({ k: INTENT.ACTIVATE, p: cmd.p, w: cmd.w, v: 0, text: '' });
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
         * `cmd.cw` carries the row's `row_action` flag: it rides WIDGET_ADD
         * because whether a row has an action is part of its shape, and a row
         * that gained or lost one is re-added rather than updated.
         */
        var name = doc.createElement('span');
        name.className = 'rowname';
        name.textContent = cmd.label || '';
        row.appendChild(name);

        if (cmd.cw) {
          var action = doc.createElement('button');
          action.type = 'button';
          action.className = 'rowact';
          action.textContent = '\u2026';
          action.title = 'Settings';
          action.addEventListener('click', function (ev) {
            ev.stopPropagation();
            self.push({ k: INTENT.ACTION, p: cmd.p, w: cmd.w, v: 0, text: '' });
          });
          row.appendChild(action);
        }

        var sw = doc.createElement('input');
        sw.type = 'checkbox';
        sw.className = 'rowsw';
        sw.addEventListener('change', function () {
          self.push({ k: INTENT.TOGGLE, p: cmd.p, w: cmd.w, v: sw.checked ? 1 : 0, text: '' });
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
        var view = doc.createElement('div');
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
        var panel = this.panels[cmd.p];
        if (panel) panel.strip = cmd.w;
        break;
      }
      default: {
        var span = doc.createElement('span');
        span.textContent = cmd.text || cmd.label || '';
        row.appendChild(span);
        entry.control = span;
        break;
      }
    }

    this.body.appendChild(row);
    this.widgets[cmd.w] = entry;
  };

  ChromeHost.prototype.apply = function (cmd) {
    if (!this.root) return;
    var w = this.widgets[cmd.w];
    var doc = this.doc;
    var panel;

    switch (cmd.k) {
      case CMD.PANEL_OPEN:
        this.panels[cmd.p] = { tabs: [], activeTab: 0, strip: -1 };
        /*
         * The panel this window is SHOWING, which is what Ok and Close name.
         *
         * Latched here because PANEL_OPEN is the only command that carries it
         * and every window has one. It used to be latched when a TABSTRIP was
         * added instead -- true of a tabbed panel, and the plugin window is
         * PAGED, not tabbed, so no strip ever arrived: the handle stayed -1,
         * both title-bar buttons addressed panel -1, and the model quietly
         * validated them away. The window could be closed from anywhere except
         * its own close mark.
         */
        this.tabPanel = cmd.p;
        if (cmd.text && this.titleEl) this.titleEl.firstChild.textContent = cmd.text;
        break;

      case CMD.PANEL_CLOSE:
        delete this.panels[cmd.p];
        this.closeDropdown();
        /* Its rows go with it: the seam says so once and means all of them,
         * so a DOM node left behind would be a control with no model. */
        for (var handle in this.widgets) {
          if (!Object.prototype.hasOwnProperty.call(this.widgets, handle)) continue;
          if (this.widgets[handle].panel !== cmd.p) continue;
          var node = this.widgets[handle].row;
          if (node.parentNode) node.parentNode.removeChild(node);
          delete this.widgets[handle];
        }
        if (this.tabPanel === cmd.p) { this.tabPanel = -1; this.renderTabs(); }
        break;

      case CMD.PANEL_TITLE:
        if (this.titleEl) this.titleEl.firstChild.textContent = cmd.text;
        break;

      case CMD.PANEL_RECT:
        /*
         * The model's box, of which the page honours the WIDTH only.
         *
         * Position is deliberately ignored: the model lays its panels out in
         * canvas coordinates, and this window is not in the canvas at all --
         * it is a frame beside it, a tab of its own, or an overlay pinned to a
         * corner. Height is ignored for a sharper reason now: docked, it is
         * the CANVAS's, because the two are meant to read as one object;
         * popped out it is the tab's; floating it scrolls under a 70vh cap.
         * None of those is the model's idea of how tall its rows came out.
         *
         * Width is different: it is the one dimension the model sizes to its
         * CONTENT (the widest row it laid out), so ignoring it is what made a
         * panel of long plugin names sit in a 340px column and ellipsise.
         * Clamped, because a model that wants 1200px still may not have it.
         *
         * It lands on whatever owns the width. Docked that is the IFRAME --
         * the root fills it, so widening the root inside a 340px frame would
         * do nothing but clip. Popped out nothing does: the tab is the user's
         * to size, and a window that resized itself under them every rebuild
         * would be a window fighting the drag.
         */
        panel = this.panels[cmd.p];
        if (panel && cmd.cw > 0 && !this.popup) {
          var want = Math.max(240, Math.min(cmd.cw + 24, 560));
          if (this.frame) {
            this.frame.style.width = want + 'px';
            this.publishDockWidth();
          } else if (this.root) {
            this.root.style.width = want + 'px';
          }
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
        }
        break;

      case CMD.WIDGET_LABEL:
        if (w && w.labelNode) w.labelNode.textContent = cmd.label;
        else if (w) {
          var lbl = w.row.querySelector('span.lbl');
          if (lbl) lbl.textContent = cmd.label;
        }
        break;

      case CMD.WIDGET_TEXT:
        if (!w) break;
        if (w.kind === W.COLORPICK) {
          /* The swatch always follows the model -- it holds no caret, so
           * there is nothing to interrupt -- while the hex field is left alone
           * whenever it has the caret, for the same reason a text input is. */
          if (w.swatch) w.swatch.value = cmd.text;
          if (doc.activeElement !== w.control) w.control.value = cmd.text;
        } else if (w.kind === W.TEXTINPUT) {
          /* Never while it has focus: the model is echoing a value the user is
           * still editing, and writing it back would move the caret and undo
           * whatever they typed since the last commit. */
          if (doc.activeElement !== w.control) w.control.value = cmd.text;
        } else if (w.kind === W.DROPDOWN && w.valueNode) {
          w.valueNode.textContent = cmd.text;
        } else if (w.control) {
          w.control.textContent = cmd.text;
        }
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
        var tint = w.labelNode || w.control;
        if (tint && tint.style)
          tint.style.color = cmd.c ? '#' + (cmd.c & 0xFFFFFF).toString(16).padStart(6, '0') : '';
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
         * their own -- see the CS2 executor. */
        break;

      default:
        break;
    }
  };

  var host = new ChromeHost();

  /* The hooks C looks for. Their PRESENCE is the availability test -- the
   * client asks before it binds, so a cached page without them degrades to
   * in-canvas chrome instead of a window that silently does nothing. */
  global.torirsChromeOpen = function () { return host.open(); };
  global.torirsChromeClose = function () { host.close(); };
  global.torirsChromeApply = function (cmd) { host.apply(cmd); };
  global.torirsChromeTakeIntent = function () { return host.takeIntent(); };
  /* The skin's three, called between open() and the first command. Their
   * absence is not an error on either side: a client that does not send them
   * leaves the window on its flat sheet, and a page that does not define them
   * is simply never called. */
  global.torirsChromeSkinMetrics = function (m) { host.skinMetrics(m); };
  global.torirsChromeSkinSprite = function (slot, w, h, b64) {
    host.skinSprite(slot, w, h, b64);
  };
  global.torirsChromeSkinDone = function () { host.skinDone(); };

  /* Exported for the node tests, which drive the same host against a fake
   * document -- no browser, no wasm, matching web/test/channel_*.js. */
  if (typeof module !== 'undefined' && module.exports)
    module.exports = {
      ChromeHost: ChromeHost, CMD: CMD, W: W, INTENT: INTENT, SKIN: SKIN,
      METRICS: METRICS
    };
})(typeof window !== 'undefined' ? window : globalThis);
