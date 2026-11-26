import pandas as pd

def remove_known_dead_csi_columns(df: pd.DataFrame) -> pd.DataFrame:
    """
    removes columns corresponding to known dead CSI subcarriers 

    - CSI data consists of 128 columns representing 64 subcarriers (real and imaginary parts).
    - column names for subcarrier 'k' are assumed to be 'k' (real) and 'ki' (imaginary).
    - dead subcarriers are: 1-6, 33, and 60-64 for my SPECIFIC AP.
    
    params:
        df: pandas dataframe containing the CSI data.
            it's assumed that CSI columns are named e.g., "1", "1i", "2", "2i", ..., "64", "64i".
            other columns like "people", "conf" are preserved (for my case).

    returns:
        pandas dataframe with the specified dead subcarrier columns removed.
    """
    # define the indices of the dead subcarriers
    dead_subcarrier_indices = []
    dead_subcarrier_indices.extend(list(range(1, 7)))  # Subcarriers 1, 2, 3, 4, 5, 6
    dead_subcarrier_indices.append(33)                 # Subcarrier 33
    dead_subcarrier_indices.extend(list(range(60, 65))) # Subcarriers 60, 61, 62, 63, 64

    columns_to_remove = []
    for sc_index in dead_subcarrier_indices:
        # generate column name for the real part (e.g., "1")
        columns_to_remove.append(str(sc_index))
        # generate column name for the imaginary part (e.g., "1i")
        columns_to_remove.append(str(sc_index) + "i")

    # identify which of these columns actually exist in the DataFrame to avoid errors
    existing_columns_to_remove = [col for col in columns_to_remove if col in df.columns]
    
    if not existing_columns_to_remove:
        print("Warning: None of the specified dead subcarrier columns were found in the DataFrame.")
        return df

    # drop the identified columns
    df_processed = df.drop(columns=existing_columns_to_remove)
    
    # print information about removed columns
    print(f"Removed {len(existing_columns_to_remove)} columns: {existing_columns_to_remove}")
    print(f"Original number of columns: {len(df.columns)}, New number of columns: {len(df_processed.columns)}")
    # number of columns should reduce by 24 if all dead subcarrier columns were present.

    return df_processed

