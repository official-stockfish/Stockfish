using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;
using NexusChess.Core;

namespace NexusChess.ViewModels
{
    public class MainWindowViewModel : INotifyPropertyChanged
    {
        private UciEngine _uciEngine;
        private ChessGame _chessGame;
        private StringBuilder _outputLog = new StringBuilder();
        private string _enginePath = "stockfish.exe";
        private string _statusMessage = "Ready";
        private string _engineStatus = "Stopped";
        private string _currentEvaluation = "";
        private bool _isEngineRunning = false;

        public event PropertyChangedEventHandler? PropertyChanged;

        #region Properties

        public string EnginePath
        {
            get => _enginePath;
            set => SetProperty(ref _enginePath, value);
        }

        public string EngineOutputLog
        {
            get => _outputLog.ToString();
        }

        public string CurrentFen => _chessGame.ToFen();

        public string StatusMessage
        {
            get => _statusMessage;
            set => SetProperty(ref _statusMessage, value);
        }

        public string EngineStatus
        {
            get => _engineStatus;
            set => SetProperty(ref _engineStatus, value);
        }

        public string CurrentEvaluation
        {
            get => _currentEvaluation;
            set => SetProperty(ref _currentEvaluation, value);
        }

        public string GameStatusText => _chessGame.SideToMove == PieceColor.White ? "White to move" : "Black to move";

        public string MoveCountText => $"Move: {_chessGame.MoveHistory.Count + 1}";

        public string CurrentTimeText => DateTime.Now.ToString("HH:mm:ss");

        public ChessGame ChessGame => _chessGame;

        public bool IsEngineRunning
        {
            get => _isEngineRunning;
            set
            {
                SetProperty(ref _isEngineRunning, value);
                ((RelayCommand)StartEngineCommand).RaiseCanExecuteChanged();
                ((RelayCommand)StopEngineCommand).RaiseCanExecuteChanged();
                ((RelayCommand)SendUciCommand).RaiseCanExecuteChanged();
                ((RelayCommand)SendIsReadyCommand).RaiseCanExecuteChanged();
                ((RelayCommand)SendGoCommand).RaiseCanExecuteChanged();
                ((RelayCommand)SendStopCommand).RaiseCanExecuteChanged();
            }
        }

        #endregion

        #region Commands

        public ICommand StartEngineCommand { get; }
        public ICommand StopEngineCommand { get; }
        public ICommand SendUciCommand { get; }
        public ICommand SendIsReadyCommand { get; }
        public ICommand SendGoCommand { get; }
        public ICommand SendStopCommand { get; }
        public ICommand NewGameCommand { get; }
        public ICommand TakeBackMoveCommand { get; }
        public ICommand ForwardMoveCommand { get; }
        public ICommand FlipBoardCommand { get; }

        #endregion

        public MainWindowViewModel()
        {
            _uciEngine = new UciEngine();
            _chessGame = new ChessGame();

            _uciEngine.OutputReceived += OnEngineOutputReceived;
            _uciEngine.EngineDisconnected += OnEngineDisconnected;

            StartEngineCommand = new RelayCommand(async _ => await StartEngineAsync(), _ => !IsEngineRunning);
            StopEngineCommand = new RelayCommand(async _ => await StopEngineAsync(), _ => IsEngineRunning);
            SendUciCommand = new RelayCommand(async _ => await _uciEngine.SendCommandAsync("uci"), _ => IsEngineRunning);
            SendIsReadyCommand = new RelayCommand(async _ => await _uciEngine.SendCommandAsync("isready"), _ => IsEngineRunning);
            SendGoCommand = new RelayCommand(async _ => await SendGoCommandAsync(), _ => IsEngineRunning);
            SendStopCommand = new RelayCommand(async _ => await _uciEngine.SendCommandAsync("stop"), _ => IsEngineRunning);
            NewGameCommand = new RelayCommand(_ => NewGame());
            TakeBackMoveCommand = new RelayCommand(_ => TakeBackMove(), _ => _chessGame.MoveHistory.Count > 0);
            ForwardMoveCommand = new RelayCommand(_ => { /* TODO: Implement forward move */ });
            FlipBoardCommand = new RelayCommand(_ => { /* TODO: Implement flip board */ });

            // Set initial status
            StatusMessage = "Welcome to NexusChess - Greetings from Jon-Arve Constantine";
            AppendOutput("NexusChess started. Greetings from Jon-Arve Constantine!");
            AppendOutput("Please set the engine path and click 'Start Engine' to begin.");
        }

        private async Task StartEngineAsync()
        {
            if (string.IsNullOrWhiteSpace(EnginePath))
            {
                StatusMessage = "Please specify engine path";
                return;
            }

            StatusMessage = "Starting engine...";
            EngineStatus = "Starting...";

            var success = await _uciEngine.StartAsync(EnginePath);
            if (success)
            {
                IsEngineRunning = true;
                EngineStatus = "Running";
                StatusMessage = "Engine started successfully";
            }
            else
            {
                EngineStatus = "Failed to start";
                StatusMessage = "Failed to start engine";
            }
        }

        private async Task StopEngineAsync()
        {
            StatusMessage = "Stopping engine...";
            EngineStatus = "Stopping...";
            await _uciEngine.StopAsync();
            IsEngineRunning = false;
            EngineStatus = "Stopped";
            StatusMessage = "Engine stopped";
        }

        private async Task SendGoCommandAsync()
        {
            // Send current position to engine
            var positionCommand = $"position fen {CurrentFen}";
            await _uciEngine.SendCommandAsync(positionCommand);
            
            // Start analysis
            await _uciEngine.SendCommandAsync("go movetime 2000");
        }

        private void NewGame()
        {
            _chessGame.NewGame();
            OnPropertyChanged(nameof(CurrentFen));
            OnPropertyChanged(nameof(GameStatusText));
            OnPropertyChanged(nameof(MoveCountText));
            OnPropertyChanged(nameof(ChessGame)); // Notify board to refresh
            StatusMessage = "New game started";
            AppendOutput("New game started.");
            
            // Update take back command availability
            ((RelayCommand)TakeBackMoveCommand).RaiseCanExecuteChanged();
        }

        private void TakeBackMove()
        {
            // TODO: Implement take back functionality
            StatusMessage = "Take back not implemented yet";
        }

        public void OnSquareClicked(string squareName)
        {
            StatusMessage = $"Clicked square: {squareName}";
            AppendOutput($"Square clicked: {squareName}");
            
            // TODO: Implement move logic
        }

        private void OnEngineOutputReceived(object? sender, string output)
        {
            AppendOutput(output);
            
            // Parse engine output for evaluation
            if (output.StartsWith("info") && output.Contains("score"))
            {
                ParseEngineInfo(output);
            }
            else if (output.StartsWith("bestmove"))
            {
                ParseBestMove(output);
            }
        }

        private void ParseEngineInfo(string infoLine)
        {
            try
            {
                var parts = infoLine.Split(' ');
                for (int i = 0; i < parts.Length - 1; i++)
                {
                    if (parts[i] == "score" && i + 2 < parts.Length)
                    {
                        if (parts[i + 1] == "cp")
                        {
                            var centipawns = int.Parse(parts[i + 2]);
                            var pawns = centipawns / 100.0;
                            CurrentEvaluation = $"Eval: {pawns:+0.00;-0.00}";
                        }
                        else if (parts[i + 1] == "mate")
                        {
                            var mateIn = int.Parse(parts[i + 2]);
                            CurrentEvaluation = $"Mate in {Math.Abs(mateIn)}";
                        }
                        break;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error parsing engine info: {ex.Message}");
            }
        }

        private void ParseBestMove(string bestMoveLine)
        {
            try
            {
                var parts = bestMoveLine.Split(' ');
                if (parts.Length >= 2)
                {
                    var bestMove = parts[1];
                    StatusMessage = $"Engine suggests: {bestMove}";
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Error parsing best move: {ex.Message}");
            }
        }

        private void OnEngineDisconnected(object? sender, EventArgs e)
        {
            IsEngineRunning = false;
            EngineStatus = "Disconnected";
            StatusMessage = "Engine disconnected";
        }

        private void AppendOutput(string message)
        {
            var timestamp = DateTime.Now.ToString("HH:mm:ss");
            _outputLog.AppendLine($"[{timestamp}] {message}");
            
            // Limit log size to prevent excessive memory usage
            if (_outputLog.Length > 50000)
            {
                var content = _outputLog.ToString();
                var startIndex = content.Length - 25000;
                _outputLog.Clear();
                _outputLog.Append(content.Substring(startIndex));
            }
            
            OnPropertyChanged(nameof(EngineOutputLog));
        }

        protected bool SetProperty<T>(ref T field, T newValue, [CallerMemberName] string? propertyName = null)
        {
            if (!Equals(field, newValue))
            {
                field = newValue;
                OnPropertyChanged(propertyName);
                return true;
            }
            return false;
        }

        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }

    // Simple RelayCommand implementation for ICommand
    public class RelayCommand : ICommand
    {
        private readonly Func<object?, Task>? _executeAsync;
        private readonly Action<object?>? _execute;
        private readonly Predicate<object?> _canExecute;

        public event EventHandler? CanExecuteChanged;

        public RelayCommand(Action<object?> execute, Predicate<object?>? canExecute = null)
        {
            _execute = execute ?? throw new ArgumentNullException(nameof(execute));
            _canExecute = canExecute ?? (_ => true);
        }

        public RelayCommand(Func<object?, Task> executeAsync, Predicate<object?>? canExecute = null)
        {
            _executeAsync = executeAsync ?? throw new ArgumentNullException(nameof(executeAsync));
            _canExecute = canExecute ?? (_ => true);
        }

        public bool CanExecute(object? parameter) => _canExecute(parameter);

        public async void Execute(object? parameter)
        {
            if (_executeAsync != null)
                await _executeAsync(parameter);
            else
                _execute?.Invoke(parameter);
        }

        public void RaiseCanExecuteChanged()
        {
            CanExecuteChanged?.Invoke(this, EventArgs.Empty);
        }
    }
}
