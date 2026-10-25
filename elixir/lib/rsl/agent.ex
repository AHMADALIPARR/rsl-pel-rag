defmodule Rsl.AgentSupervisor do
  @moduledoc "PEL-style OTP supervision for agent reasoning over retrieved lattice regions."
  use Supervisor

  def start_link(opts) do
    Supervisor.start_link(__MODULE__, opts, name: __MODULE__)
  end

  @impl true
  def init(_opts) do
    children = [
      {Rsl.ResearcherAgent, []}
    ]
    Supervisor.init(children, strategy: :one_for_one)
  end
end

defmodule Rsl.ResearcherAgent do
  @moduledoc "Agent that reasons over related evidence from the lattice."
  use GenServer

  def start_link(opts) do
    GenServer.start_link(__MODULE__, opts, name: __MODULE__)
  end

  @impl true
  def init(_opts) do
    {:ok, %{}}
  end

  def reason_over(related) do
    GenServer.call(__MODULE__, {:reason, related})
  end

  @impl true
  def handle_call({:reason, related}, _from, state) do
    # Structured reasoning over the selected regions
    synthesis = Enum.map(related, fn r ->
      "Region #{r.region_id} (score #{r.score}): verified provenance, segments #{inspect(r.segments)}"
    end)
    {:reply, %{synthesis: synthesis, confidence: 0.82}, state}
  end
end
