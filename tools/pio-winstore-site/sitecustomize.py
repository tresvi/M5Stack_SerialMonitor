# Solo se usa al compilar con PYTHONPATH apuntando a esta carpeta:
# hace que todo Python (incluidos los subprocesos de PlatformIO/SCons) valide HTTPS
# contra el almacen de certificados de Windows. Ver ..\pio_winstore.py
try:
    import truststore

    truststore.inject_into_ssl()
except Exception:
    pass
