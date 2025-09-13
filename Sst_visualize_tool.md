# SST File Visualization Tool

This is a Python script that parses a custom SST (Sorted String Table) file and generates an interactive, self-contained HTML report to visualize its internal structure and data.

## Requirements
- Python 3+ (no external libraries needed)

## Usage

Run the script from your command line, passing the path to an SST file as an argument:

```bash
python3 sst_visualize.py /path/to/your/file.sst
```

## Output

The script will create a directory named `sst_html` in your current folder and save the HTML report inside it.

For example, after analyzing `000123.sst`, you can find the report at `sst_html/000123.sst_visualization.html`. Open this file in any web browser to view the visualization.