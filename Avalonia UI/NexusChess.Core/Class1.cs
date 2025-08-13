using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

namespace NexusChess.Core
{
    public class UciEngine : IDisposable
    {
        // Event to notify GUI of engine output
        public event EventHandler<string>? OutputReceived;
        public event EventHandler? EngineDisconnected;

        private Process? _engineProcess;
        private StreamWriter? _engineInput;
        private bool _isRunning;
        private bool _disposed;

        public bool IsRunning => _isRunning && _engineProcess != null && !_engineProcess.HasExited;

        public async Task<bool> StartAsync(string enginePath)
        {
            try
            {
                if (_isRunning)
                {
                    await StopAsync();
                }

                if (!File.Exists(enginePath))
                {
                    OutputReceived?.Invoke(this, $"Error: Engine executable not found at {enginePath}");
                    return false;
                }

                _engineProcess = new Process
                {
                    StartInfo = new ProcessStartInfo
                    {
                        FileName = enginePath,
                        UseShellExecute = false,
                        RedirectStandardInput = true,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true
                    }
                };

                _engineProcess.OutputDataReceived += EngineProcess_OutputDataReceived;
                _engineProcess.ErrorDataReceived += EngineProcess_ErrorDataReceived;
                _engineProcess.Exited += EngineProcess_Exited;
                _engineProcess.EnableRaisingEvents = true;

                if (_engineProcess.Start())
                {
                    _engineInput = _engineProcess.StandardInput;
                    _engineProcess.BeginOutputReadLine();
                    _engineProcess.BeginErrorReadLine();
                    _isRunning = true;

                    // Send initial UCI command
                    await SendCommandAsync("uci");
                    OutputReceived?.Invoke(this, $"Engine started: {enginePath}");
                    return true;
                }
                else
                {
                    OutputReceived?.Invoke(this, "Failed to start engine process");
                    return false;
                }
            }
            catch (Exception ex)
            {
                OutputReceived?.Invoke(this, $"Error starting engine: {ex.Message}");
                return false;
            }
        }

        public async Task SendCommandAsync(string command)
        {
            if (_engineInput != null && _isRunning)
            {
                try
                {
                    await _engineInput.WriteLineAsync(command);
                    await _engineInput.FlushAsync();
                    OutputReceived?.Invoke(this, $"> {command}");
                }
                catch (Exception ex)
                {
                    OutputReceived?.Invoke(this, $"Error sending command: {ex.Message}");
                }
            }
        }

        public async Task StopAsync()
        {
            if (_isRunning && _engineProcess != null)
            {
                try
                {
                    await SendCommandAsync("quit");
                    _isRunning = false;

                    // Give the process time to exit gracefully
                    if (!_engineProcess.WaitForExit(3000))
                    {
                        _engineProcess.Kill();
                    }
                }
                catch (Exception ex)
                {
                    OutputReceived?.Invoke(this, $"Error stopping engine: {ex.Message}");
                }
                finally
                {
                    _engineProcess?.Dispose();
                    _engineProcess = null;
                    _engineInput = null;
                }
            }
        }

        private void EngineProcess_OutputDataReceived(object sender, DataReceivedEventArgs e)
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                OutputReceived?.Invoke(this, e.Data);
            }
        }

        private void EngineProcess_ErrorDataReceived(object sender, DataReceivedEventArgs e)
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                OutputReceived?.Invoke(this, $"ERROR: {e.Data}");
            }
        }

        private void EngineProcess_Exited(object sender, EventArgs e)
        {
            _isRunning = false;
            EngineDisconnected?.Invoke(this, EventArgs.Empty);
            OutputReceived?.Invoke(this, "Engine process exited");
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                StopAsync().Wait(5000);
                _disposed = true;
            }
            GC.SuppressFinalize(this);
        }
    }
}
