# Proyecto 1 IO - Fabian Bustos - Esteban Secaida

Pequeño proyecto en C con dos ejecutables (`menu` y `pending`) usando **GTK+ 3**. Se compilan con `make` y se guardan en `bin/`.

Proyecto en C con un menu Principal. La primera opción ahora se encuentra disponible. Esta ejecuta un **algoritmo de Floyd** para las rutas más cortas 

La segunda opción del menu ahora se encuentra disponible. Esta ejecuta el **Problema de la Mochila** en sus diferentes variantes: 1/0, bounded y unbounded. 

## Requisitos

- Linux (probado en Ubuntu/Mint)
- `gcc`, `make`
- `pkg-config`
- Headers de **GTK+ 3**

### Instalación de dependencias

**Ubuntu / Debian / Linux Mint**
```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev
sudo apt install texlive-latex-recommended texlive-latex-extra texlive-fonts-recommended
```
### Comandos de Make 
- Compilar todos los archivos: *make all*
- Correr el menu principa: *make run-menu*
- Correr el programa de Floyd: *make run-floyd*
- Limpiar archivos: *make clean*