#!/usr/bin/python3
import requests
from bs4 import BeautifulSoup
import pandas as pd
import datetime

# URL of contributors
url = "https://tests.stockfishchess.org/contributors"

response = requests.get(url)

if response.status_code == 200:
    soup = BeautifulSoup(response.text, 'html.parser')
    table = soup.find('table', {'id': 'contributors_table'})
    # Empty list to store the rows
    rows = []

    for row in table.find_all('tr'):
        cols = row.find_all('td')
        if len(cols) > 0:
            rows.append((cols[1].text.strip(), cols[2].text.strip(), cols[3].text.strip(), cols[4].text.strip(), cols[5].text.strip(), cols[6].text.strip(), cols[7].text.strip()))

    # DataFrame with the rows
    df = pd.DataFrame(rows, columns=['Username', 'Last active', 'Games/Hour', 'CPU Hours', 'Games played', 'Tests submitted', 'Tests repository'])

    # Filtering DataFrame to get only users with more than 10,000 CPU Hours
    df['CPU Hours'] = df['CPU Hours'].astype(int)
    filtered_df = df[df['CPU Hours'] > 10000]
    
    # Sorting DataFrame by 'CPU Hours' in desc order
    sorted_df = filtered_df.sort_values(by='CPU Hours', ascending=False)
    
    current_date = datetime.datetime.now()
    formatted_date = current_date.strftime('%Y-%m-%d')

    document = f"Contributors to Fishtest with >10,000 CPU hours, as of {formatted_date}.\nThank you!\n\n"
    document += "Username                                CPU Hours     Games played\n"
    document += "------------------------------------------------------------------\n"
    
    for index, row in sorted_df.iterrows():
        document += "{:<34} {:>13} {:>17}\n".format(row['Username'], row['CPU Hours'], row['Games played'])
    print(document)
    
    ## Copy generated content to a text file named Top CPU Contributors.txt
    with open("Top CPU Contributors.txt", "w") as file:
        file.write(document)
    
else:
    print("Web not found.")
