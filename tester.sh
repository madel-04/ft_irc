#!/usr/bin/env bash

set -euo pipefail
HOST="${1:-127.0.0.1}"
PORT="${2:-1234}"
LOGFILE="irc_test.log"

# Usuarios de prueba
NICK1="alice"
USER1="alice"
NICK2="ghost42"
USER2="ghost"

CHANNEL="#testchannel"

: > "$LOGFILE"
echo "=== IRC tester log - $(date -u +%Y-%m-%dT%H:%M:%SZ) ===" >> "$LOGFILE"

# helper para log con timestamp y tag
log_append() {
  local tag="$1"
  local line="$2"
  printf '%s [%s] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$tag" "$line" >> "$LOGFILE"
}

# Abre dos sockets usando /dev/tcp si está disponible
if [[ ! -e /dev/tcp/$HOST/$PORT ]]; then
  # /dev/tcp no aparece como archivo normal; we'll just try to open sockets and fall back if fails
  true
fi

# FD 3 = cliente A (tester), FD 4 = cliente B (ghost)
exec 3<>/dev/tcp/"$HOST"/"$PORT" || { echo "No puedo conectar a $HOST:$PORT (fd3)"; exit 1; }
exec 4<>/dev/tcp/"$HOST"/"$PORT" || { echo "No puedo conectar a $HOST:$PORT (fd4)"; exit 1; }

# Lectores en background que vacían lo que venga de cada FD al log
{
  while IFS=$'\r' read -r -u 3 line; do
    # read -u 3 lee hasta \n; los \r quedan en line o se gestionan según servidor; limpiamos
    line="${line%$'\r'}"
    log_append "RECV-A" "$line"
  done
} & reader_a_pid=$!

{
  while IFS=$'\r' read -r -u 4 line; do
    line="${line%$'\r'}"
    log_append "RECV-B" "$line"
  done
} & reader_b_pid=$!

# Funciones para enviar (asegurando CRLF) y loggear
send_a() {
  local s="$1"
  printf '%s\r\n' "$s" >&3
  log_append "SEND-A" "$s"
}

send_b() {
  local s="$1"
  printf '%s\r\n' "$s" >&4
  log_append "SEND-B" "$s"
}

# Secuencia de comandos (simple, lineal). Espera un poco entre comandos para que el servidor responda.
sleep 0.2
send_a "NICK $NICK1"
send_a "USER $USER1"
sleep 0.8

send_b "NICK $NICK2"
send_b "USER $USER2"
sleep 1

# Tester crea/join canal
send_a "JOIN $CHANNEL"
sleep 0.6

# Invitar al ghost al canal (INVITE)
send_a "INVITE $NICK2 $CHANNEL"
sleep 0.6

# Cerrar sesión ambos
send_b "QUIT"
sleep 0.3
send_a "QUIT"

# Espera para recibir lo último
sleep 1

# Cerrar file descriptors y matar readers
exec 3>&-
exec 3<&-
exec 4>&-
exec 4<&-

# dar tiempo a que los readers terminen y luego matarlos si siguen vivos
sleep 0.2
kill "$reader_a_pid" "$reader_b_pid" 2>/dev/null || true

echo "Secuencia completada. Log en: $LOGFILE"
echo "Contenido (últimas 40 líneas):"
tail -n 40 "$LOGFILE"