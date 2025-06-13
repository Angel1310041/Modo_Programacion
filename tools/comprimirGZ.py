# Comprimir un archivo HTML a formato GZ con interfaz gráfica

import tkinter as tk
from tkinter import filedialog
import gzip
import shutil

def seleccionar_archivo():
    archivo = filedialog.askopenfilename(filetypes=[("HTML files", "*.html")])
    if archivo:
        entrada_var.set(archivo)

def seleccionar_carpeta():
    carpeta = filedialog.askdirectory()
    if carpeta:
        salida_var.set(carpeta)

def comprimir_archivo():
    archivo_entrada = entrada_var.get()
    carpeta_salida = salida_var.get()
    if archivo_entrada and carpeta_salida:
        archivo_salida = f"{carpeta_salida}/{archivo_entrada.split('/')[-1]}.gz"
        with open(archivo_entrada, 'rb') as f_in:
            with gzip.open(archivo_salida, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)
        resultado_var.set(f"Archivo comprimido: {archivo_salida}")
    else:
        resultado_var.set("Seleccione un archivo y una carpeta de destino")

# Configuración de la interfaz gráfica
root = tk.Tk()
root.title("Comprimir HTML a GZ")

entrada_var = tk.StringVar()
salida_var = tk.StringVar()
resultado_var = tk.StringVar()

tk.Label(root, text="Archivo HTML:").grid(row=0, column=0, padx=10, pady=10)
tk.Entry(root, textvariable=entrada_var, width=50).grid(row=0, column=1, padx=10, pady=10)
tk.Button(root, text="Seleccionar archivo", command=seleccionar_archivo).grid(row=0, column=2, padx=10, pady=10)

tk.Label(root, text="Carpeta de destino:").grid(row=1, column=0, padx=10, pady=10)
tk.Entry(root, textvariable=salida_var, width=50).grid(row=1, column=1, padx=10, pady=10)
tk.Button(root, text="Seleccionar carpeta", command=seleccionar_carpeta).grid(row=1, column=2, padx=10, pady=10)

tk.Button(root, text="Comprimir", command=comprimir_archivo).grid(row=2, column=1, padx=10, pady=10)
tk.Label(root, textvariable=resultado_var).grid(row=3, column=0, columnspan=3, padx=10, pady=10)

root.mainloop()
