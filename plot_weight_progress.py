import matplotlib.pyplot as plt
import numpy as np

def read_file(filename):
    with open(filename, 'r') as file:
        header = file.readline().strip().split()
        data = [line.strip().split() for line in file.readlines()]
    return header, data

def plot_data(data, k, subtract_initial=False, display_column=None, display_range=None):
    data = np.array(data, dtype=float).T  # Transpose data to work with columns

    if display_column is not None:
        columns_to_plot = [display_column]
    elif display_range is not None:
        columns_to_plot = list(range(display_range[0], display_range[1] + 1))
    else:
        columns_to_plot = range(data.shape[0])

    for i in columns_to_plot:
        column = data[i]
        if subtract_initial:
            initial_value = column[0]
            column = column - initial_value
        
        # Calculate the averages of the last k datapoints
        averaged_column = [np.mean(column[j:j+k]) for j in range(0, len(column), k)]
        
        plt.plot(averaged_column, label=f'Column {i+1}')
    
    plt.xlabel('Averaged Segments')
    plt.ylabel('Value')
    plt.title(f'Data from File (Averaged over last {k} points)')
    plt.legend()
    plt.show()

def main():
    filename = 'History_of_Weights.txt'  # Replace with your file name

    # Options
    k = 10                  # Number of datapoints to average
    subtract_initial = True  # Set to True to subtract the initial value
    display_column = None    # Set to the column number (0-based index) to display a specific column, or None to display all columns
    display_range = (899, 899+5)   #from 4 it gets interesting

    header, data = read_file(filename)
    print(f'Header: {header}')
    plot_data(data, k, subtract_initial=subtract_initial, display_column=display_column, display_range=display_range)

if __name__ == "__main__":
    main()
