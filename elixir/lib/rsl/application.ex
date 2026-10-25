defmodule Rsl.Application do
  @moduledoc false
  use Application

  @impl true
  def start(_type, _args) do
    children = [
      {Rsl.AgentSupervisor, []},
      {Rsl.QueryEngine, []}
    ]

    opts = [strategy: :one_for_one, name: Rsl.Supervisor]
    Supervisor.start_link(children, opts)
  end
end
