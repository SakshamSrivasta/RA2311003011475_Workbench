# Handwritten backpropagation appendix (max 2 pages)

Put the scans/photos of the handwritten derivation here, e.g. `page1.jpg`, `page2.jpg` (or one PDF). The brief asks for:

1. Forward pass: z, h, ŷ, J for x = 1, y = 2, w = 0.5, b = 0, v = 1, c = 0.
2. Chain-rule derivatives ∂J/∂w, ∂J/∂b, ∂J/∂v, ∂J/∂c.
3. One simultaneous update with learning rate 0.1, using gradients from the original parameters.
4. A short explanation of the gradient direction.

Values to check your hand calculation against (from `wb_tests` / `results/backprop_gradient_check.csv`):

| Quantity | Value |
|---|---|
| z = wx + b | 0.5 |
| h = tanh z | 0.4621172 |
| ŷ = vh + c | 0.4621172 |
| e = ŷ − y | −1.5378828 |
| J = ½e² | 1.1825418 |
| ∂J/∂c = e | −1.5378828 |
| ∂J/∂v = e·h | −0.7106820 |
| ∂J/∂b = e·v·(1 − h²) | −1.2094645 |
| ∂J/∂w = e·v·(1 − h²)·x | −1.2094645 |
| w, b, v, c after one step | 0.6209464, 0.1209464, 1.0710682, 0.1537883 |
| J after one step | 0.6857738 |

These numbers are only for checking. The derivation itself has to be written by hand, and you should be able to explain every step in the verification discussion.
