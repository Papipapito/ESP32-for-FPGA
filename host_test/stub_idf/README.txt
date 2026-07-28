stub_idf - cabeceras MINIMAS de ESP-IDF v5.5 para comprobar en el PC que la
PARTE B de XInputHost.cpp (el transporte USB) compila contra las firmas REALES.

QUE PRUEBA ESTO Y QUE NO
  SI  - que cada llamada existe y con los tipos correctos: si alguien se
        inventa un parametro o cambia un tipo, aqui revienta.
  NO  - nada del comportamiento en ejecucion. El USB no se simula. La unica
        verificacion real del transporte es la placa.

Las firmas estan TRANSCRITAS de ESP-IDF v5.5, no inventadas:
  components/usb/include/usb/usb_host.h
  components/usb/include/usb/usb_types_stack.h
  components/usb/include/usb/usb_types_ch9.h
  components/usb/include/usb/usb_helpers.h
https://github.com/espressif/esp-idf/tree/release/v5.5/components/usb/include/usb

Solo esta lo que usa XInputHost.cpp. Si el fichero empieza a usar mas API,
habra que anadirla aqui copiandola de la misma fuente, NUNCA de memoria.
