import json
from pathlib import Path
import os

def load_app_config(project_root: Path, config_filename: str = "config.json"):
    """
    loads the configuration file config.json from the project root.

    parameters:
        project_root (Path): path to the project's root directory.
        config_filename (str): name of the configuration file.

    returns:
        dict: the loaded configuration.
    """
    config_path = project_root / config_filename
    if not config_path.exists():
        raise FileNotFoundError(f"Configuration file not found at {config_path}")
    with open(config_path, 'r') as f:
        config = json.load(f)
    return config

def get_raw_data_path(project_root: Path, config: dict, category_key: str, sub_category_key: str) -> Path:
    """
    constructs the full path for a specific raw data category and sub-category.

    parameters:
        project_root (Path): absolute path to the project's root directory.
        config (dict): loaded configuration dictionary.
        category_key (str): key for the main category (e.g., "0_persons").
        sub_category_key (str): key for the sub-category (e.g., "left").

    returns:
        Path: absolute path to the specified raw data directory.
    """
    # paths defined in config.json are relative to the project_root
    base_path = project_root / config["data_dir"] / config["raw_data_subdir"]
    
    try:
        category_path_part = config["raw_data_categories"][category_key][sub_category_key]
    except KeyError:
        raise KeyError(f"Category '{category_key}' or sub-category '{sub_category_key}' not found in config['raw_data_categories'].")
        
    return base_path / category_path_part

def get_processed_data_path(project_root: Path, config: dict, processing_step_key: str, category_key: str, sub_category_key: str, create_if_not_exists: bool = True) -> Path:
    """
    constructs the full output path for processed data for a specific step, 
    category, and sub-category. Optionally creates the directory if it doesn't exist.

    parameters:
        project_root (Path): absolute path to the project's root directory.
        config (dict): loaded configuration dictionary.
        processing_step_key (str): key for the processing step (e.g., "column_removal").
        category_key (str): key for the main category (e.g., "0_persons").
        sub_category_key (str): key for the sub-category (e.g., "left").
        create_if_not_exists (bool): if True, creates the directory if it doesn't exist.

    returns:
        Path: absolute path to the specified processed data directory.
              this path points to the directory where files for this step and category should be saved.
    """
    # paths defined in config.json are relative to the project_root
    base_processed_path = project_root / config["data_dir"] / config["processed_data_subdir"]
    
    try:
        step_output_folder_name = config["processing_steps_outputs"][processing_step_key]
    except KeyError:
        raise KeyError(f"Processing step '{processing_step_key}' not found in config['processing_steps_outputs'].")
        
    try:
        # this part constructs the subdirectory structure like "0_pers/left" 
        # that will be replicated within the step's output folder.
        relative_category_structure = config["raw_data_categories"][category_key][sub_category_key]
    except KeyError:
        raise KeyError(f"Category '{category_key}' or sub-category '{sub_category_key}' not found in config['raw_data_categories'] for processed path construction.")

    full_path = base_processed_path / step_output_folder_name / relative_category_structure
    
    if create_if_not_exists:
        full_path.mkdir(parents=True, exist_ok=True) 
        
    return full_path