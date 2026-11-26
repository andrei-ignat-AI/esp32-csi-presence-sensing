import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from pathlib import Path
from typing import List, Optional, Dict, Tuple
from .utils import separate_metadata 


# consistent styling for plots
sns.set_theme(style="whitegrid")
PLOT_DPI = 300 # Resolution for saved plots

def ensure_plot_dir(base_plot_dir: Path, step_name: str, category_key: str, sub_category_key: str) -> Path:
    """Ensures the specific plot directory exists and returns its path."""
    plot_dir = base_plot_dir / step_name / category_key / sub_category_key
    plot_dir.mkdir(parents=True, exist_ok=True)
    return plot_dir