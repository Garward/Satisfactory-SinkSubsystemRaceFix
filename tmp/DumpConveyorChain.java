// Decompile vanilla conveyor chain replication/save flow.
//@category SinkRaceFix

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class DumpConveyorChain extends GhidraScript {
	private static final String[] PATTERNS = new String[] {
		"AFGBuildableConveyorBase::OnRep_ChainInformation",
		"AFGConveyorChainActor::RegisterWithSubsystem",
		"AFGConveyorChainActor::AddClientAvailableConveyor",
		"AFGConveyorChainActor::NetUpdateBuildSpline",
		"AFGConveyorChainActor::BuildChain",
		"AFGConveyorChainActor::MoveItemsFromBeltsToChain",
		"AFGConveyorChainActor::PostLoadGame_Implementation",
		"AFGConveyorChainSubsystem::NotifyChainNeedsSegmentUpdate",
		"AFGConveyorChainSubsystem::NotifyChainReceiveSegmentUpdate",
		"AFGConveyorChainSubsystem::NotifyChainNeedsItemUpdate",
		"AFGConveyorChainSubsystem::NotifyChainReceivedItemUpdate",
		"UFGConveyorChainRemoteCallObject::Server_RequestChainSegmentData",
		"UFGConveyorChainRemoteCallObject::Client_RespondChainSegmentData",
		"UFGConveyorChainRemoteCallObject::Server_RequestChainItemUpdate",
		"UFGConveyorChainRemoteCallObject::Client_RespondChainItemUpdate",
		"UFGConveyorChainSubsystemReplicationComponent::BeginPlay",
		"UFGConveyorChainSubsystemReplicationComponent::InitializeAsClient",
		"UFGConveyorChainSubsystemReplicationComponent::SendInitialReplicationData",
		"UFGConveyorChainSubsystemReplicationComponent::ReceiveInitialReplicationData"
	};

	private static boolean matches(String name) {
		for (String pat : PATTERNS) {
			if (name.contains(pat)) return true;
		}
		return false;
	}

	@Override
	public void run() throws Exception {
		String outPath = "/mnt/e/Claude/Satisfactory/SinkSubsystemRaceFix/tmp/decompiled_conveyor_chain.txt";
		PrintWriter w = new PrintWriter(new FileWriter(outPath));

		DecompInterface decomp = new DecompInterface();
		decomp.setOptions(new DecompileOptions());
		decomp.openProgram(currentProgram);

		List<Function> targets = new ArrayList<>();
		Set<String> seen = new HashSet<>();
		FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
		while (fit.hasNext() && !monitor.isCancelled()) {
			Function f = fit.next();
			String name = f.getName(true);
			if (!matches(name)) continue;
			String key = f.getEntryPoint().toString();
			if (seen.add(key)) targets.add(f);
		}

		w.println("# Decompiled conveyor-chain function dump");
		w.println("# Total functions matched: " + targets.size());
		w.println();

		for (Function f : targets) {
			if (monitor.isCancelled()) break;
			w.println("// =========================================================");
			w.println("// " + f.getName(true));
			w.println("// Entry: " + f.getEntryPoint());
			w.println("// =========================================================");
			DecompileResults res = decomp.decompileFunction(f, 90, monitor);
			if (res != null && res.getDecompiledFunction() != null) {
				w.println(res.getDecompiledFunction().getC());
			} else {
				w.println("// (decompile failed: " + (res != null ? res.getErrorMessage() : "null result") + ")");
			}
			w.println();
		}

		w.close();
		decomp.dispose();
		println("Wrote " + targets.size() + " functions to " + outPath);
	}
}
