# csi presence detection - python processing & training

this project contains the python code for processing the csi data collected by the esp32, extracting features, and training and evaluating a multi-layer perceptron (mlp) model for binary presence detection.

the core workflow is implemented across three separate jupyter notebooks, each representing a different data labeling strategy.

## project structure

-   **`data/raw_data/`**: this is where the raw `.csv` files from the esp32's sd card should be placed. the structure is important for the scripts to assign ground truth.
    -   `0_pers/`: contains data collected when the room was empty.
    -   `1_pers/`: contains data collected when one person was present.
    -   each of these has subfolders like `left/` and `right/` for data from different device placements.

-   **`notebooks/01_forced_ground_truth.ipynb`**: notebook for the **forced ground truth (fgt)** experiment.
-   **`notebooks/02_raw_detector_labels.ipynb`**: notebook for the **raw detector labels (rdl)** experiment. also contains the cells for generating the final paper and presentation plots.
-   **`notebooks/03_confidence_filtered_labels.ipynb`**: notebook for the **confidence-filtered detector labels (cfdl)** experiment.

-   **`csi_preprocessing/`**: this is a python module with helper functions. note that the main processing logic for these experiments has been defined locally within each notebook to ensure they are self-contained. this was done to lock in the exact pipeline for the paper. the functions in this module might not be used by these specific notebooks, so for this project, you can focus on the code inside the notebooks themselves.

-   **`config.json`**: a central file that defines data paths, subcarrier info, and experiment names.

-   **`experiment_outputs_[suffix]/`**: each notebook creates its own output directory. this keeps the cleaned data, processing artifacts (scaler, pca model), and final windowed data for each experiment completely separate.

-   **`saved_models/`** & **`plots/`**: these directories store the final trained models and evaluation plots, again organized into subfolders based on the experiment.

## setup and execution

1.  **environment**: make sure you have a python environment with the necessary libraries installed. key libraries include:
    -   `pandas`
    -   `numpy`
    -   `scikit-learn`
    -   `tensorflow`
    -   `pywavelets` (for pywt)
    -   `matplotlib`
    -   `seaborn`
    -   `joblib`

2.  **data**: place your raw `.csv` files from the esp32 into the `data/raw_data/` directory, following the folder structure described above.

3.  **configuration**: review `config.json` to ensure the paths and column names match your setup.

4.  **run notebooks**: open and run each jupyter notebook (`notebooks/01_forced_ground_truth.ipynb`, `notebooks/02_raw_detector_labels.ipynb`, `notebooks/03_confidence_filtered_labels.ipynb`) cell by cell. each notebook is a self-contained experiment from data cleaning to model training and evaluation.

## processing pipeline

all notebooks follow the same core data processing and feature engineering pipeline, as described in the paper. the only difference between them is how the labels are handled.

1.  **data loading and cleaning**:
    -   raw `.csv` files are loaded.
    -   metadata columns (`conf`, `det ts`, `csi ts`) are dropped.
    -   12 known inactive subcarriers (and their corresponding imaginary parts) are removed. this reduces the initial 128 csi values down to 104 features per timestep.

2.  **labeling strategy**: this is the key step that differs between notebooks.
    -   **fgt (`notebooks/01_forced_ground_truth.ipynb`)**: labels in the `people` column are **overwritten**. all rows get a `0` or `1` based on the folder name (`0_pers` or `1_pers`), creating a perfectly clean dataset.
    -   **rdl (`notebooks/02_raw_detector_labels.ipynb`)**: the original `people` column from the esp32's detector (with values like 0, 1, 2...) is kept as is.
    -   **cfdl (`notebooks/03_confidence_filtered_labels.ipynb`)**: any row with a detected person (`people > 0`) but a confidence score below a set threshold has its `people` value changed to `0`. this cleans up low-quality detections before training. the threshold is set in cell 1 of the notebook.

3.  **scaler & pca fitting**:
    -   to prevent data leakage, a `standardscaler` and a `pca` model are fitted **only on the data from the training set files**.
    -   these fitted artifacts are saved and then used to transform the train, validation, and test sets consistently.

4.  **dwt denoising**:
    -   a discrete wavelet transform (`db4` wavelet, level 4) is used to denoise the csi signals.
    -   this is applied to the 104 scaled features *before* the pca step.

5.  **principal component analysis (pca)**:
    -   pca is used to reduce the dimensionality of the 104 dwt-denoised features.
    -   it is configured to retain 99% of the variance, which results in approximately 12 principal components.

6.  **temporal windowing**:
    -   the resulting time-series of principal components is segmented into overlapping windows. each window has 16 timesteps with a stride of 8.
    -   a single binary label is assigned to each window. if any frame within the window is labeled as presence (people > 0), the entire window gets the label `1`, otherwise it is `0`.

7.  **mlp training**:
    -   the windowed data is flattened to create a 1d vector for each sample.
    -   this vector is used to train a 4-hidden-layer mlp, as detailed in the paper. the training uses the adam optimizer, class weights to handle imbalance, and callbacks for early stopping and learning rate reduction.

## notebooks in detail

#### `notebooks/01_forced_ground_truth.ipynb` (forced ground truth)
-   **purpose**: to establish a performance benchmark with perfect labels.
-   **key action**: overwrites the `people` column based on the folder name during data cleaning. this shows the maximum potential of the csi features and processing pipeline.

#### `notebooks/02_raw_detector_labels.ipynb` (raw detector labels)
-   **purpose**: to simulate a fully automated labeling scenario without any manual correction or filtering.
-   **key action**: uses the `people` column directly from the esp32. the mapping from multi-class (0, 1, 2...) to binary (0, 1) happens during the windowing step.

#### `notebooks/03_confidence_filtered_labels.ipynb` (confidence-filtered labels)
-   **purpose**: to test if pre-filtering the noisy labels using their confidence score can improve model performance.
-   **key action**: during data cleaning, it looks at the `conf` column. if a detection's confidence is below the `confidence_threshold` set in cell 1, its label is reset to `0`. this effectively removes false positives before they enter the training pipeline.