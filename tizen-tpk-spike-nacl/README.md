# tizen-tpk-spike-nacl

Spike: um `.wgt` com um `.nexe` ARM (Native Client, Samsung NaCl SDK pepper_63)
para TVs Samsung Tizen 4/5 (2018/2019), onde o `.tpk` com `.so` propria e
barrado pelo UEP.

O que prova, numa tela so: quadrado girando em GLES2 (PPB_Graphics3D), um
pthread, e um HTTPS feito pela pagina (ponte postMessage). Azul = esperando,
verde = HTTPS ok, vermelho = falhou. Texto de diagnostico no canto.

STATUS: compila e linka (arm-nacl-clang newlib, 819 KB). NAO foi executado em
TV nem em nenhum runtime NaCl: nao ha Chrome com NaCl neste Mac.

Build: `NACL_SDK_ROOT=<pepper_63> bash build.sh` (ver o cabecalho do script
para baixar o SDK). Sai `out/NuvioNaclSpike.wgt`, sem assinatura.

Instalar: assinar/reassinar para o DUID da TV (Apps2Samsung) e instalar como
qualquer .wgt. Se a pagina mostrar `plugin x-nacl: false`, a TV nao tem NaCl
habilitado: veredito negativo. Se mostrar `nacl error`, o texto do erro diz se
foi o .nmf, o .nexe ou o sandbox.
