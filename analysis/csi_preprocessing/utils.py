import pandas as pd
from typing import List, Tuple

def separate_metadata(df: pd.DataFrame, metadata_columns: List[str]) -> Tuple[pd.DataFrame, pd.DataFrame]:
    """separates data into CSI features and metadata."""
    if not all(col in df.columns for col in metadata_columns):
        # identify missing columns 
        missing_cols = [col for col in metadata_columns if col not in df.columns]
        present_cols = df.columns.tolist()

        existing_metadata_cols = [col for col in metadata_columns if col in df.columns]
        if not existing_metadata_cols: # if NO metadata columns exist, return empty df for metadata
             return df.copy(), pd.DataFrame(index=df.index)

        csi_df = df.drop(columns=existing_metadata_cols, errors='ignore')
        meta_df = df[existing_metadata_cols].copy()
        return csi_df, meta_df

    csi_df = df.drop(columns=metadata_columns)
    meta_df = df[metadata_columns].copy()
    return csi_df, meta_df

def get_config_csi_metadata_cols(config: dict) -> List[str]:
    """helper to get metadata columns from config"""
    return config.get("csi_metadata_columns", ["people", "conf", "csi ts", "det ts"])