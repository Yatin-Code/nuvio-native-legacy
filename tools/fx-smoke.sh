#!/bin/bash
# Smoke do fixture: sobe o app com fixture, espera injetar, vai para home.
set -u
cd "$(dirname "$0")/.."
for p in $(pgrep -f 'build/linux/nuvio-native-legacy' 2>/dev/null); do
  if [ "$(readlink "/proc/$p/cwd" 2>/dev/null)" = "$PWD" ]; then kill "$p" 2>/dev/null || true; fi
done
sleep 1
rm -f /tmp/nuvio-shot-req /tmp/nuvio-shot.bmp /tmp/nuvio-key /tmp/nuvio-fixture
rm -rf /tmp/fx-test && mkdir -p /tmp/fx-test/dados
for m in novidades-11 novidades-12 novidades-13 novidades-131 novidades-132 \
    novidades-133 novidades-134 novidades-139 novidades-14-celebracao \
    novidades-142 novidades-150 novidades-152-ui novidades-160-ui \
    novidades-170-ui novidades-172-ui novidades-174-ui novidades-180-ui \
    novidades-20-guia novidades-201 novidades-202 novidades-guia \
    aviso-log registro-aberto salvos-intro; do
  printf '1\n' > /tmp/fx-test/dados/$m.txt
done
printf 'versao=1\n' > /tmp/fx-test/dados/diagnostico-otimizacao-intro.cfg
printf '0\n' > /tmp/fx-test/dados/telemetria-perguntado.txt
export NUVIO_UI_PREVIEW=1 NUVIO_FIXTURE=1 NUVIO_DADOS=/tmp/fx-test/dados
setsid nohup xvfb-run -a -s "-screen 0 1920x1080x24" \
  ./build/linux/nuvio-native-legacy ./deploy/app/art \
  > /tmp/fx-test/app.log 2>&1 < /dev/null &
echo "smoke: app PID $!"
