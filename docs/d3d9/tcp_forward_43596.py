"""Bridge 10.10.10.1:{43595,43596} -> 127.0.0.1:43596.

torirsserver binds INADDR_LOOPBACK only and serves JS5 and the game on one
port (first byte 15 vs 14). The XP manifest names two ports on 10.10.10.1, so
both listeners pipe to the same upstream.
"""
import socket, sys, threading

UP = ('127.0.0.1', 43596)

def pipe(a, b):
    try:
        while True:
            d = a.recv(65536)
            if not d:
                break
            b.sendall(d)
    except Exception:
        pass
    finally:
        for s in (a, b):
            try: s.shutdown(socket.SHUT_RDWR)
            except Exception: pass
            try: s.close()
            except Exception: pass

def serve(conn):
    try:
        up = socket.create_connection(UP, timeout=10)
    except Exception as e:
        sys.stderr.write('upstream refused: %r\n' % (e,)); sys.stderr.flush()
        conn.close(); return
    up.settimeout(None)
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    up.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    threading.Thread(target=pipe, args=(conn, up), daemon=True).start()
    threading.Thread(target=pipe, args=(up, conn), daemon=True).start()

def listener(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('10.10.10.1', port))
    s.listen(16)
    sys.stderr.write('fwd: listening on 10.10.10.1:%d -> %s:%d\n' % (port, UP[0], UP[1]))
    sys.stderr.flush()
    while True:
        c, _ = s.accept()
        threading.Thread(target=serve, args=(c,), daemon=True).start()

for p in (43596,):
    threading.Thread(target=listener, args=(p,), daemon=True).start()
threading.Event().wait()
