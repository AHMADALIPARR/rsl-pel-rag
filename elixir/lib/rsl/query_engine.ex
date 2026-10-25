defmodule Rsl.QueryEngine do
  @moduledoc """
  Query-directed traversal using SQL predicates and PEL agent context.
  Selects regions before expensive similarity.
  """

  use GenServer

  def start_link(opts \\ []) do
    GenServer.start_link(__MODULE__, opts, name: __MODULE__)
  end

  @impl true
  def init(_opts) do
    {:ok, %{lattices: %{}}}
  end

  def execute(query_string, question, context \\ %{}) do
    GenServer.call(__MODULE__, {:execute, query_string, question, context})
  end

  @impl true
  def handle_call({:execute, query_string, question, context}, _from, state) do
    case Rsl.QueryParser.parse(query_string) do
      {:ok, parsed} ->
        # In production: call Rust NIF for signature of question, traverse lattice
        # Here: structural placeholder that demonstrates the flow
        evidence = traverse_lattice(parsed, question, context)
        related = join_source_registry(evidence)
        result = %{
          evidence: evidence,
          related: related,
          agent: "researcher",
          reason_over: related
        }
        {:reply, {:ok, result}, state}
      {:error, reason} ->
        {:reply, {:error, reason}, state}
    end
  end

  defp traverse_lattice(parsed, question, _context) do
    # Deterministic sparse signature of question would be computed in Rust
    # Then lattice.traverse_candidates(sig, top)
    top = Keyword.get(parsed.clauses, :top, 16) || 16
    # Placeholder regions matching the syntax contract
    [
      %{region_id: 1, score: 0.87, segments: [42, 43], provenance: %{verified: true}},
      %{region_id: 2, score: 0.74, segments: [17], provenance: %{verified: true}}
    ]
    |> Enum.take(top)
  end

  defp join_source_registry(evidence) do
    # SQL join as specified
    Enum.map(evidence, fn e ->
      Map.put(e, :source, %{id: e.region_id, registry: "verified"})
    end)
  end
end
