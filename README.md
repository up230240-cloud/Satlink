Proyecto Satlink

Este proyecto es de codigo abierto el cual contiene el codigo para el control de la sonda aerea Satlink en colaboracion con Tepeyac y UPA. 

El codigo funciona con una esp32 en la placa de Lyligo T-beam Supreme, se transmiten los datos de temperatura, humedad, altura, localizacion y trazabilidad general a traves de protocolo LoRaWan en sub banda 2 para su correcta transmision a las unidades de recepcion del ITPA.

El codigo esta escrito en C++ con configuraciones en python y js para el control con platformio IDE, la base es Arduino Core para su correcto entendimiento. Las librerias usadas son de codigo abierto con licencia MIT. 
