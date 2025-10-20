
# Práctica 2 de laboratorio - Unix Shell

## Integrantes:

### Miguel Angel Puerta Vasquez - 1000760164
### N/A - 

## Desarrollo

Se realizo la practica dentro del archivo llamado wish.c, este se encarga de iniciar un unix shell y de permitir la ejecucion de comandos de manera interactiva por el usuario, los comandos tambien pueden ser ingresados a travez de un archivo de entrada que llame a los comandos que se deberian de ejecutar.

Esta practica posee 1 documentos de prueba para probar la funcionalidad del programa:
- batch.txt

El batch se encarga de ejecutar los siguientes comandos en orden:

```
path /bin
ls > lista.txt
pwd > out.txt
cd ..
pwd
echo 123 & echo 456 & echo 789
cd SO-Lab2-20241
sleep 5 & echo sleeping
cat out.txt
echo done
```

Donde define el nuevo path list para los comandos dentro del /bin, luego lee los archivos dentro de la carpeta en la que estamos usando ls y lo envia a un output llamado lista.txt, lee la ubicacion actual con pwd y lo envia a un out.txt, procedera a moverese al nivel inferior y mirar la carpeta en la que estamos, realizara una ejecucion en paralelo de 3 comandos echo y volvera a entrar a la carpeta de desarrollo, ejecuta un sleep y en paralelo le notifica al usuario que esta durmiendo, finalmente lee el documento creado al inicio y le notifica al usuario que termino.


Este Shell tendra dos metodos de uso para el usuario, el modo interactivo y el modo batch, para iniciar el modo interactivo usaremos el comando:

```sh
prompt> ./wish
```

Este entrara en el modo interactivo y permitira que el usuario ingrese cada comando que el quiera ejecutar uno por uno, los comandos permitidos por el shell seran cd, exit y path como built-in commands dentro del mismo, y todos los comandos que se encuentren dentro de /bin inicialmente o cualquier otro path que el usuario indique, a su vez dentro de este mismo se permite el uso de comandos en paralelo mediante el uso de '&' entre los comandos y redirigir el output de un comando a un archivo de salida mediante '>' y el nombre del output. 

Si durante la ejecucion se obtiene un error o el usuario tiene un error de sintaxis, se llamara al mensaje de error "An error has occurred" y se imprimira a stdout para que el usuario pueda ver los errores ocurridos.

A su vez se permitira el llamado directo al shell dado un archivo de entrada, haciendo esto se ejecutaran todos los comandos dentro del archivo de entrada que se le de al shell en orden de ejecucion, y para usar el metodo de esa manera usaremos el comando:

```sh
prompt> ./wish batch.txt
```

Esto permitira ejecutar varias lineas de comandos rapidamente en vez de tener que escribir una por una
