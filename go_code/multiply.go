package main

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

func runAsChild() {
	input, err := io.ReadAll(os.Stdin)
	if err != nil {
		fmt.Println("Error reading stdin:", err)
		return
	}

	parts := strings.SplitN(string(input), "---\n", 2)
	if len(parts) != 2 {
		fmt.Println("Invalid input format")
		return
	}

	aChunk := deserializeMatrix(parts[0])
	b := deserializeMatrix(parts[1])
	result := multiplyMatricesSequential(aChunk, b)
	fmt.Print(serializeMatrix(result))
}

func loadMatrix(route string) [][]float64 {
	file, err := os.Open(route)

	if err != nil {
		fmt.Println("Error to open the file", err)
		return nil
	}
	defer file.Close()

	var matrix [][]float64
	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		line := scanner.Text()
		numbers := strings.Fields(line)
		var row []float64
		for _, number := range numbers {
			num, err := strconv.ParseFloat(number, 64)
			if err != nil {
				continue
			}
			row = append(row, num)
		}
		matrix = append(matrix, row)

	}

	return matrix
}

func verifyMultiplicity(a, b [][]float64) (string, error) {

	numberOfRowsA := len(a)
	numberOfColumnsA := len(a[0])
	numberOfRowsB := len(b)

	if numberOfRowsB != numberOfColumnsA {
		return "", errors.New("matrices must comply with A columns number equals to B rows number")
	}

	return fmt.Sprintln(numberOfRowsA, numberOfColumnsA), nil
}

func multiplyMatricesSequential(a, b [][]float64) [][]float64 {
	aRows := len(a)
	aColumns := len(a[0])
	bColumns := len(b[0])

	result := make([][]float64, aRows)
	for i := 0; i < aRows; i++ {
		result[i] = make([]float64, bColumns)
		for j := 0; j < bColumns; j++ {
			sum := 0.0
			for k := 0; k < aColumns; k++ {
				sum += a[i][k] * b[k][j]
			}
			result[i][j] = sum
		}
	}
	return result
}

func serializeMatrix(matrix [][]float64) string {

	var result strings.Builder
	for _, row := range matrix {
		for j, val := range row {
			result.WriteString(fmt.Sprintf("%f", val))
			if j < len(row)-1 {
				result.WriteString(" ")
			}
		}
		result.WriteString("\n")
	}
	return result.String()

}

func deserializeMatrix(data string) [][]float64 {
	lines := strings.Split(strings.TrimSpace(data), "\n")
	var matrix [][]float64

	for _, line := range lines {
		fields := strings.Fields(line)
		var row []float64
		for _, val := range fields {
			num, err := strconv.ParseFloat(val, 64)
			if err != nil {
				continue
			}
			row = append(row, num)
		}
		matrix = append(matrix, row)
	}

	return matrix
}

func splitMatrixRows(a [][]float64, numProcesses int) [][][]float64 {
	aRows := len(a)
	chunkSize := aRows / numProcesses
	remainder := aRows % numProcesses

	var result [][][]float64
	start := 0

	for i := 0; i < numProcesses; i++ {
		end := start + chunkSize
		if i < remainder {
			end++
		}
		result = append(result, a[start:end])
		start = end
	}

	return result
}

func multiplyMatricesParallel(a, b [][]float64, numProcesses int) [][]float64 {

	chunks := splitMatrixRows(a, numProcesses)
	bSerialized := serializeMatrix(b)
	results := make([][]float64, 0)

	for _, chunk := range chunks {
		cmd := exec.Command(os.Args[0], "--child")

		stdin, err := cmd.StdinPipe()
		if err != nil {
			fmt.Println("Error getting stdin pipe:", err)
			return nil
		}

		stdout, err := cmd.StdoutPipe()
		if err != nil {
			fmt.Println("Error getting stdout pipe:", err)
			return nil
		}

		if err := cmd.Start(); err != nil {
			fmt.Println("Error starting child process:", err)
			return nil
		}

		go func() {
			defer stdin.Close()
			data := serializeMatrix(chunk) + "---\n" + bSerialized
			stdin.Write([]byte(data))
		}()

		output, err := io.ReadAll(stdout)
		if err != nil {
			fmt.Println("Error reading child output:", err)
			return nil
		}

		partialResult := deserializeMatrix(string(output))
		results = append(results, partialResult...)

		cmd.Wait()
	}

	return results
}

func askForMethod(a [][]float64) int {
	fmt.Print("\nType in the number of processes you want to use for the application (greater than 1): ")
	reader := bufio.NewReader(os.Stdin)
	input, _ := reader.ReadString('\n')
	input = strings.TrimSpace(input)

	numProcesses, err := strconv.Atoi(input)
	if err != nil {
		fmt.Println("Invalid number, defaulting to 2 proccess.")
		return 2
	}

	if numProcesses == 1 {
		fmt.Printf("The number of process have to be greater than 1. Adjusting to 2 processes.\n")
		return 2
	} else if numProcesses > len(a) {
		fmt.Printf("You have more processes than rows in matrix A. Adjusting to %d processes.\n", len(a))
		return len(a)
	}
	return numProcesses
}

func writeMatrixToFile(matrix [][]float64, filePath string) error {
	file, err := os.Create(filePath)
	if err != nil {
		return fmt.Errorf("could not create file: %v", err)
	}
	defer file.Close()

	writer := bufio.NewWriter(file)

	for _, row := range matrix {
		var line []string
		for _, val := range row {
			line = append(line, fmt.Sprintf("%.2f", val))
		}
		_, err := writer.WriteString(strings.Join(line, " ") + "\n")
		if err != nil {
			return fmt.Errorf("error writing to file: %v", err)
		}
	}
	return writer.Flush()
}

func main() {

	if len(os.Args) > 1 && os.Args[1] == "--child" {
		runAsChild()
		return
	}

	a := loadMatrix("./matrices/A.txt")
	b := loadMatrix("./matrices/B.txt")
	_, err := (verifyMultiplicity(a, b))
	if err != nil {
		fmt.Println("Error: ", err)
		return
	}
	numProcesses := askForMethod(a)
	var startSequential time.Time
	var startParallel time.Time
	startSequential = time.Now()
	_ = multiplyMatricesSequential(a, b)
	durationSequential := time.Since(startSequential)
	startParallel = time.Now()
	resultParallel := multiplyMatricesParallel(a, b, numProcesses)
	durationParallel := time.Since(startParallel)
	_ = writeMatrixToFile(resultParallel, "./matrices/C.txt")
	fmt.Printf("Sequential time: %f seconds", durationSequential.Seconds())
	fmt.Printf("\nParallel time (%d): %f seconds", numProcesses, durationParallel.Seconds())
	fmt.Printf("\nSpeedup: %fX \n", durationSequential.Seconds()/durationParallel.Seconds())
}
