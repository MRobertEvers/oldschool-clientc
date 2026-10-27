/*
 * The login form, for the browser's password manager.
 *
 * The title's form is drawn on the canvas, and a password manager only ever
 * fills and saves real <input>s. So this lays a real <form> -- a username and a
 * current-password input -- over the two drawn fields while the client shows
 * them (platform_credential_form.h), and keeps both sides in step:
 *
 *   client -> page   Module.torirsCredentialForm(state), whenever the form
 *                    changes: where the fields are, which has the caret, the
 *                    username, and whether the password was cleared
 *   page -> client   _torirs_credential_input(field, text) on every change
 *                    (typing, or the browser filling), _torirs_credential_focus
 *                    and _torirs_credential_submit (Enter)
 *
 * The inputs are transparent -- the canvas still draws the form, caret and
 * all -- but they are where the clicks and keys land, so the browser offers
 * its saved logins on focus and fills them. Keys typed into them stop here
 * (except Escape, the form's own way back), or SDL would type them a second
 * time into the client's field.
 *
 * When the login goes through, the page offers to save: a submit event the
 * password manager watches for, and navigator.credentials.store where the
 * Credential Management API exists.
 */
(function () {
  'use strict';

  const FIELDS = ['username', 'password'];
  let form = null;
  let inputs = null;
  let state = null;
  /* What this page last told the client each field holds, so a periodic sync
   * pushes only a value that changed HERE -- never overwriting what was typed
   * into the client while no input had the focus. */
  const pushed = ['', ''];
  /* The credentials of the login in flight, kept for the save offer: the
   * client clears its password the moment it submits. */
  let submitted = null;
  let wasConnecting = false;
  let wasInGame = false;

  function call(name, types, args) {
    if (typeof Module === 'undefined' || typeof Module.ccall !== 'function') { return; }
    Module.ccall(name, null, types, args);
  }

  function push(field) {
    const value = inputs[field].value;
    if (value === pushed[field]) { return; }
    pushed[field] = value;
    call('torirs_credential_input', ['number', 'string'], [field, value]);
  }

  function pushBoth() {
    if (!inputs || !state || !state.showing) { return; }
    push(0);
    push(1);
  }

  function build() {
    form = document.createElement('form');
    form.id = 'torirs-credentials';
    form.setAttribute('autocomplete', 'on');
    form.style.cssText = 'position:fixed;left:0;top:0;margin:0;padding:0;display:none;z-index:5;';
    inputs = FIELDS.map((name, field) => {
      const input = document.createElement('input');
      input.name = name;
      input.type = field === 0 ? 'text' : 'password';
      input.autocomplete = field === 0 ? 'username' : 'current-password';
      input.setAttribute('autocapitalize', 'off');
      input.setAttribute('autocorrect', 'off');
      input.spellcheck = false;
      input.setAttribute('aria-label', field === 0 ? 'Username' : 'Password');
      /* Invisible but real: the canvas draws the text and the caret. The
       * autofill rules keep a browser's fill colour off the canvas too. */
      input.style.cssText =
        'position:fixed;margin:0;padding:0;border:0;outline:none;background:transparent;' +
        'color:transparent;caret-color:transparent;-webkit-text-fill-color:transparent;' +
        'box-shadow:none;font:12px sans-serif;';
      input.addEventListener('input', () => push(field));
      input.addEventListener('change', () => push(field));
      input.addEventListener('focus', () => {
        push(field);
        call('torirs_credential_focus', ['number'], [field]);
      });
      /* Enter in the username goes on to the password, as the client's own
       * form does (RS_Title_HandleKey); only the password's Enter submits. */
      if (field === 0) {
        input.addEventListener('keydown', (event) => {
          if (event.key !== 'Enter') { return; }
          event.preventDefault();
          inputs[1].focus({ preventScroll: true });
        });
      }
      for (const type of ['keydown', 'keypress', 'keyup']) {
        input.addEventListener(type, (event) => {
          /* Escape goes on to the client, which leaves the form. Everything
           * else is this input's alone. */
          if (event.key !== 'Escape') { event.stopPropagation(); }
        });
      }
      form.appendChild(input);
      return input;
    });
    const submit = document.createElement('button');
    submit.type = 'submit';
    submit.tabIndex = -1;
    submit.style.cssText = 'position:fixed;left:-10000px;width:1px;height:1px;';
    form.appendChild(submit);
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      /* A login that already went through, re-submitted below so the
       * password manager sees a submission: nothing more to do. */
      if (form.dataset.saving === '1') { return; }
      pushBoth();
      call('torirs_credential_submit', [], []);
    });
    const style = document.createElement('style');
    style.textContent =
      '#torirs-credentials input:-webkit-autofill{transition:background-color 100000s 0s;' +
      '-webkit-text-fill-color:transparent;}';
    document.head.appendChild(style);
    document.body.appendChild(form);

    /* A browser may fill a password without telling the page until the
     * person does something (Chrome withholds the value until a gesture), and
     * the click that follows is often on the canvas's own Login button. So
     * every gesture, and a slow tick, re-reads both inputs first. */
    document.addEventListener('pointerdown', pushBoth, true);
    document.addEventListener('keydown', pushBoth, true);
    setInterval(() => { pushBoth(); place(); }, 300);
    window.addEventListener('resize', place);
    window.addEventListener('scroll', place, true);
  }

  /* The inputs over the drawn fields: the client's boxes are canvas CSS
   * pixels, the inputs are positioned in the viewport. */
  function place() {
    if (!form || !state || !state.showing) { return; }
    const canvas = document.getElementById('canvas');
    if (!canvas) { return; }
    const rect = canvas.getBoundingClientRect();
    state.fields.forEach((box, field) => {
      const input = inputs[field];
      input.style.left = `${rect.left + box.x}px`;
      input.style.top = `${rect.top + box.y}px`;
      input.style.width = `${Math.max(1, box.w)}px`;
      input.style.height = `${Math.max(1, box.h)}px`;
    });
  }

  function offerSave(credentials) {
    /* A submission the password manager can see, then the form goes away --
     * the pair most of them take as "that login worked". */
    form.dataset.saving = '1';
    if (typeof form.requestSubmit === 'function') { form.requestSubmit(); }
    form.dataset.saving = '';
    if (window.PasswordCredential && navigator.credentials && credentials.password) {
      try {
        const credential = new window.PasswordCredential({
          id: credentials.username, password: credentials.password, name: credentials.username,
        });
        navigator.credentials.store(credential).catch(() => {});
      } catch (err) {
        /* No Credential Management here (or not in this context): the
         * submission above is the whole offer. */
      }
    }
  }

  Module.torirsCredentialForm = function (next) {
    if (!form) { build(); }
    const wasShowing = state && state.showing;
    state = next;

    /* The login is in flight: keep what was submitted, before the client's
     * cleared password reaches the inputs below. */
    if (next.connecting && !wasConnecting) {
      submitted = { username: inputs[0].value, password: inputs[1].value };
    }
    if (next.inGame && !wasInGame && submitted && submitted.username) {
      offerSave(submitted);
      submitted = null;
    }
    wasConnecting = next.connecting;
    wasInGame = next.inGame;

    /* The client moved the form on its own (a prefill, a cancel, a cleared
     * password): an input not being typed into follows it. */
    if (document.activeElement !== inputs[0] && inputs[0].value !== next.username) {
      inputs[0].value = next.username;
      pushed[0] = next.username;
    }
    if (next.passwordLen === 0 && inputs[1].value !== '') {
      inputs[1].value = '';
      pushed[1] = '';
    }

    if (!next.showing) {
      if (form.contains(document.activeElement)) { document.activeElement.blur(); }
      form.style.display = 'none';
      return;
    }
    form.style.display = 'block';
    place();
    /* The client's caret is the focus: keys go where it is drawn, and the
     * browser offers its saved logins on that input. */
    const wanted = inputs[next.focus === 1 ? 1 : 0];
    if (!wasShowing || document.activeElement !== wanted) {
      if (document.hasFocus()) { wanted.focus({ preventScroll: true }); }
    }
  };
})();
