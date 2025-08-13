using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Input;
using System;
using System.Diagnostics;
using System.Linq;
using NexusChess.ViewModels;
using NexusChess.Core;

namespace NexusChess.Desktop;

public partial class MainWindow : Window
{
    private Grid? _chessBoard;
    private const int BoardSize = 8;
    private readonly IBrush _lightSquareColor = new SolidColorBrush(Color.Parse("#F0D9B5"));
    private readonly IBrush _darkSquareColor = new SolidColorBrush(Color.Parse("#B58863"));
    private readonly IBrush _highlightColor = new SolidColorBrush(Color.Parse("#FFFF00"));
    private MainWindowViewModel? _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
        InitializeChessBoard();
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            _viewModel = viewModel;
            // Update the board when the view model changes
            UpdateBoardFromGame();
        }
    }

    private void InitializeChessBoard()
    {
        // Find the chess board grid in the XAML
        _chessBoard = this.FindControl<Grid>("ChessBoard");
        if (_chessBoard == null) return;

        // Set up 8x8 grid structure
        _chessBoard.RowDefinitions.Clear();
        _chessBoard.ColumnDefinitions.Clear();
        
        for (int i = 0; i < BoardSize; i++)
        {
            _chessBoard.RowDefinitions.Add(new RowDefinition(GridLength.Star));
            _chessBoard.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
        }

        // Create board squares
        for (int row = 0; row < BoardSize; row++)
        {
            for (int col = 0; col < BoardSize; col++)
            {
                var square = new Border
                {
                    Background = (row + col) % 2 == 0 ? _lightSquareColor : _darkSquareColor,
                    BorderBrush = Brushes.Transparent,
                    BorderThickness = new Thickness(1)
                };

                // Create a grid for the square content
                var squareGrid = new Grid();
                
                // Add coordinate labels
                if (row == BoardSize - 1) // Bottom row - file labels
                {
                    var fileLabel = new TextBlock
                    {
                        Text = ((char)('a' + col)).ToString(),
                        FontSize = 10,
                        FontWeight = FontWeight.Bold,
                        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
                        VerticalAlignment = Avalonia.Layout.VerticalAlignment.Bottom,
                        Margin = new Thickness(0, 0, 2, 2),
                        Foreground = (row + col) % 2 == 0 ? _darkSquareColor : _lightSquareColor
                    };
                    squareGrid.Children.Add(fileLabel);
                }

                if (col == 0) // Left column - rank labels
                {
                    var rankLabel = new TextBlock
                    {
                        Text = (8 - row).ToString(),
                        FontSize = 10,
                        FontWeight = FontWeight.Bold,
                        HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left,
                        VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top,
                        Margin = new Thickness(2, 2, 0, 0),
                        Foreground = (row + col) % 2 == 0 ? _darkSquareColor : _lightSquareColor
                    };
                    squareGrid.Children.Add(rankLabel);
                }

                square.Child = squareGrid;

                // Add click handler for square selection
                square.PointerPressed += Square_PointerPressed;
                square.Tag = $"{(char)('a' + col)}{8 - row}"; // Store square name

                Grid.SetRow(square, row);
                Grid.SetColumn(square, col);
                _chessBoard.Children.Add(square);
            }
        }

        UpdateBoardFromGame();
    }

    private void UpdateBoardFromGame()
    {
        if (_chessBoard == null || _viewModel?.ChessGame == null) return;

        var game = _viewModel.ChessGame;

        for (int row = 0; row < BoardSize; row++)
        {
            for (int col = 0; col < BoardSize; col++)
            {
                var square = GetSquareAt(row, col);
                if (square?.Child is Grid squareGrid)
                {
                    // Remove existing piece if any
                    var pieceLabel = squareGrid.Children.OfType<TextBlock>()
                        .FirstOrDefault(t => t.FontSize > 10);
                    if (pieceLabel != null)
                        squareGrid.Children.Remove(pieceLabel);

                    // Get piece from game state (note: game uses file,rank while display uses row,col)
                    var piece = game.GetPiece(new Square(col, 7 - row)); // Convert display coordinates to game coordinates
                    
                    // Add piece if not empty
                    if (!piece.IsEmpty)
                    {
                        var pieceSymbol = new TextBlock
                        {
                            Text = piece.ToUnicodeSymbol(),
                            FontSize = 36,
                            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                            Foreground = Brushes.Black
                        };
                        squareGrid.Children.Add(pieceSymbol);
                    }
                }
            }
        }
    }

    private Border? GetSquareAt(int row, int col)
    {
        return _chessBoard?.Children
            .OfType<Border>()
            .FirstOrDefault(b => Grid.GetRow(b) == row && Grid.GetColumn(b) == col);
    }

    private void Square_PointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is Border square && square.Tag is string squareName)
        {
            Debug.WriteLine($"Clicked on square: {squareName}");
            
            // Pass the click to the view model
            _viewModel?.OnSquareClicked(squareName);
        }
    }

    // Add method to refresh board from view model
    public void RefreshBoard()
    {
        UpdateBoardFromGame();
    }
}