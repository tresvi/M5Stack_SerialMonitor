"""Ejecuta PlatformIO validando HTTPS con el almacen de certificados de Windows.

En la red corporativa el proxy re-firma HTTPS con la CA del banco, que esta
instalada en Windows pero no en la lista propia de Python (certifi).
truststore hace que Python verifique contra Windows (igual que pip y el
navegador): la verificacion sigue completa, solo cambia la fuente de confianza.

Uso: python pio_winstore.py run -e m5stack-cardputer
"""
import sys

import truststore

truststore.inject_into_ssl()

from platformio.__main__ import main  # noqa: E402

sys.exit(main())
