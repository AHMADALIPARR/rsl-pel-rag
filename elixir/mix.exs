defmodule Rsl.MixProject do
  use Mix.Project

  def project do
    [
      app: :rsl,
      version: "0.1.0",
      elixir: "~> 1.16",
      start_permanent: Mix.env() == :prod,
      deps: deps()
    ]
  end

  def application do
    [
      extra_applications: [:logger],
      mod: {Rsl.Application, []}
    ]
  end

  defp deps do
    [
      {:postgrex, "~> 0.18"},
      {:jason, "~> 1.4"},
      {:nimble_parsec, "~> 1.4"}
    ]
  end
end
