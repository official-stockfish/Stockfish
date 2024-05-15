#!/usr/bin/python3
import requests
from bs4 import BeautifulSoup
import pandas as pd
import datetime

# URL de la página web
url = "https://tests.stockfishchess.org/contributors"

# Realizamos la petición a la web
response = requests.get(url)

# Comprobamos que la petición nos devuelve un Status Code = 200
if response.status_code == 200:
    # Pasamos el contenido HTML de la web a un objeto BeautifulSoup()
    soup = BeautifulSoup(response.text, 'html.parser')

    # Obtenemos la tabla por un ID específico
    table = soup.find('table', {'id': 'contributors_table'})

    # Creamos una lista vacía para almacenar nuestras filas
    rows = []

    # Recorremos cada fila
    for row in table.find_all('tr'):
        # Obtenemos las columnas
        cols = row.find_all('td')
        # Si hay columnas
        if len(cols) > 0:
            # Añadimos a nuestra lista una tupla con los valores de las columnas
            rows.append((cols[1].text.strip(), cols[2].text.strip(), cols[3].text.strip(), cols[4].text.strip(), cols[5].text.strip(), cols[6].text.strip(), cols[7].text.strip()))

    # Creamos un DataFrame con nuestras filas
    df = pd.DataFrame(rows, columns=['Username', 'Last active', 'Games/Hour', 'CPU Hours', 'Games played', 'Tests submitted', 'Tests repository'])

    # Mostramos el DataFrame
#    print(df)
    # Filtramos el DataFrame para obtener solo los usuarios con más de 10,000 CPU Hours
    df['CPU Hours'] = df['CPU Hours'].astype(int)
    filtered_df = df[df['CPU Hours'] > 10000]
    
    # Ordenamos el DataFrame por 'CPU Hours' en orden descendente
    sorted_df = filtered_df.sort_values(by='CPU Hours', ascending=False)
    
    # Obtenemos la fecha actual
    current_date = datetime.datetime.now()
    
    # Formateamos la fecha al formato deseado (YYYY-MM-DD)
    formatted_date = current_date.strftime('%Y-%m-%d')

    # Creamos el documento
    document = f"Contributors to Fishtest with >10,000 CPU hours, as of {formatted_date}.\nThank you!\n\n"
    document += "Username                                CPU Hours     Games played\n"
    document += "------------------------------------------------------------------\n"
    
    for index, row in sorted_df.iterrows():
        document += f"{row['Username']}                               {row['CPU Hours']}       {row['Games played']}\n"
    
    print(document)
    
    ## Copiamos el encontenido de la variable documento a un archivo de texto llamado Top CPU Contributors.txt
    with open("Top CPU Contributors.txt", "w") as file:
        file.write(document)
    
else:
    print("No se pudo obtener la página web.")

