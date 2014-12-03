#!/usr/bin/python
"""Python module for generating .ninja files."""
import textwrap

def escape_path(word):
    return word.replace('$ ', '$$ ').replace(' ', '$ ').replace(':', '$:')

class Writer(object):
    def __init__(self, output, width=78):
        self.output = output
        self.width = width

    def __del__(self):
        self.output.close()

    def newline(self):
        self.output.write('\n')

    def variable(self, key, value, indent=0):
        self._line('%s = %s' % (key, value), indent)

    def rule(self, name, command, description=None, depfile=None, deps=None):
        self._line('rule %s' % name)
        self.variable('command', command, indent=1)
        if description:
            self.variable('description', description, indent=1)
        if depfile:
            self.variable('depfile', depfile, indent=1)
        if deps:
            self.variable('deps', deps, indent=1)

    def build(self, outputs, rule, inputs=None):
        out_outputs = [escape_path(x) for x in outputs]
        all_inputs = [escape_path(x) for x in inputs]
        self._line('build %s: %s' % (' '.join(out_outputs), ' '.join([rule] + all_inputs)))
        return outputs

    def default(self, paths):
        self._line('default %s' % ' '.join(paths))

    def _line(self, text, indent=0):
        """Write 'text' word-wrapped at self.width characters."""
        lines = textwrap.wrap(text, width = self.width - 2,
            initial_indent = '    ' * indent, subsequent_indent = '    ' * (indent + 1))
        for line in lines[:-1]:
            self.output.write(line + ' $\n')
        self.output.write(lines[-1] + '\n')
