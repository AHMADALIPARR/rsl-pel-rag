defmodule Rsl.QueryParser do
  @moduledoc """
  Native query syntax for RAG LATTICE.
  Parses:
  LET evidence = RAG LATTICE documents
    SEGMENT BY INFORMATION_BOUNDARY
    SIGNATURE SPARSE_HASH64
    TRAVERSE RECURSIVE
    WHERE provenance.verified = TRUE
    MATCH :question
    TOP 16;
  """

  import NimbleParsec

  whitespace = ascii_string([?\s, ?\t, ?\n], min: 1)

  identifier =
    ascii_string([?a..?z, ?A..?Z, ?_], 1)
    |> repeat(ascii_string([?a..?z, ?A..?Z, ?0..?9, ?_], min: 1))
    |> reduce({List, :to_string, []})

  let_binding =
    ignore(string("LET"))
    |> ignore(whitespace)
    |> concat(identifier)
    |> ignore(whitespace)
    |> ignore(string("="))
    |> ignore(whitespace)

  rag_lattice =
    ignore(string("RAG"))
    |> ignore(whitespace)
    |> ignore(string("LATTICE"))
    |> ignore(whitespace)
    |> concat(identifier)
    |> optional(ignore(whitespace) |> ignore(string("SEGMENT")) |> ignore(whitespace) |> ignore(string("BY")) |> ignore(whitespace) |> concat(identifier))
    |> optional(ignore(whitespace) |> ignore(string("SIGNATURE")) |> ignore(whitespace) |> concat(identifier))
    |> optional(ignore(whitespace) |> ignore(string("TRAVERSE")) |> ignore(whitespace) |> concat(identifier))
    |> optional(ignore(whitespace) |> ignore(string("WHERE")) |> ignore(whitespace) |> ascii_string([not: ?\n], min: 1))
    |> optional(ignore(whitespace) |> ignore(string("MATCH")) |> ignore(whitespace) |> concat(identifier))
    |> optional(ignore(whitespace) |> ignore(string("TOP")) |> ignore(whitespace) |> integer(min: 1))

  defparsec(:parse_query, let_binding |> concat(rag_lattice) |> eos())

  def parse(query_string) when is_binary(query_string) do
    case parse_query(query_string) do
      {:ok, [binding | rest], "", _, _, _} ->
        {:ok, %{binding: binding, clauses: rest}}
      {:error, reason, _, _, _, _} ->
        {:error, reason}
    end
  end
end
