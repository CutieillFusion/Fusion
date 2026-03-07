# MicroGPT

A character-level GPT language model implemented entirely in Fusion. Trains on a dataset of names and generates new ones.

## Files

- `microgpt.fusion` — Main program: training loop + inference
- `gpt.fusion` — GPT model: embeddings, multi-head attention, MLP, softmax
- `value.fusion` — Autograd `Value` type with forward ops (add, neg, log, scale)
- `autograd.fusion` — Backward pass with topological sort and automatic memory management
- `names.fusion` — Dataset loading, tokenization, and vocab building
- `rand.fusion` — Gaussian and uniform random number generation
- `input.txt` — Training data (one name per line)

## Running

From the project root:

```
./build/compiler/fusion examples/microgpt/microgpt.fusion
```

Build the compiler first if needed:

```
./make.sh
```

## Configuration

Hyperparameters are set at the top of `microgpt.fusion`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `n_embd` | 16 | Embedding dimension |
| `n_head` | 4 | Number of attention heads |
| `head_dim` | 4 | Dimension per head |
| `block_size` | 16 | Max sequence length |
| `num_steps` | 1000 | Training iterations |
| `learning_rate` | 0.01 | Initial learning rate (linear decay) |
| `n_inference_samples` | 20 | Number of names to generate |

## Output

During training, each line prints the step number and loss. After training, inference generates names as sequences of ASCII character codes.
