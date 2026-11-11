# Market-data capture documentation

The four design documents separate implemented behavior, agreed direction and open proposals.
They describe responsibilities and data contracts independently of module placement.

| Read first | Document | Purpose |
| --- | --- | --- |
| 1 | [MD capture reference design](md_capture_reference_design.md) | Capture structure, rationale and current limitations |
| 2 | [Backend processing](backend_processing.md) | Independent services, hot-hot redundancy and reconciliation |
| 3 | [Improvement proposal](improvement_proposal.md) | Data packages, precise time rotation, recovery and remaining decisions |
| 4 | [Market data services](md_services.md) | Raw and derived datasets, symbol history, retention and delivery |

- [Restart checklist](../../TODO.md): work to resume after design review.
- [Project overview](../../README.md): purpose, key components, dependencies, build and license.
- [MD logger implementation guide](md_logger.md): current API, configuration and failure handling.
- [Application control](app_control.md): lifecycle state machine, events, signals and transition tables.
- [Applications](../../apps/README.md): current capture applications and their operating guides.
- [Application-layer plan](https://github.com/nomisma-labs/handbook/blob/master/master-plan/application-layer.md): platform architecture and ownership boundaries.
- [Shared build documentation](../kit/README.md): build instructions, conventions and recipes.
- [Repository instructions](../../AGENTS.md): agent guidance.
